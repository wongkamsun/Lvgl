#include "mrker.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

#include "lvgl/lvgl.h"

#ifdef _WIN32
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "Ws2_32.lib")
#else
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <sys/socket.h>
  #include <sys/types.h>
  #include <unistd.h>
#endif

namespace {

struct MrkerCache {
    bool connected = false;
    int http_code = -1;
    uint32_t last_ok_tick = 0;

    float extruder_temp = NAN;
    float extruder_target = NAN;
    float bed_temp = NAN;
    float bed_target = NAN;

    float progress01 = -1.0f;

    char state[32] = {0};
    char filename[160] = {0};
    char message[200] = {0};
};

std::mutex g_mu;
MrkerCache g_cache;

std::atomic<bool> g_running{false};
std::thread g_thr;

std::string g_base_url = "http://127.0.0.1:7125";
std::string g_api_key;
char g_base_url_cstr[256] = {0};

static void strlcpy0(char* dst, size_t dst_sz, const std::string& s)
{
    if (!dst || dst_sz == 0) return;
    const size_t n = std::min(dst_sz - 1, s.size());
    if (n) std::memcpy(dst, s.data(), n);
    dst[n] = '\0';
}

struct UrlParts {
    std::string host;
    std::string port;
    std::string base_path;
    bool ok = false;
};

static UrlParts parse_http_base_url(const std::string& base_url)
{
    UrlParts p;
    std::string s = base_url;
    while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(s.begin());
    while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();

    const std::string kHttp = "http://";
    if (s.rfind(kHttp, 0) == 0) s.erase(0, kHttp.size());
    else return p; // 仅支持 http

    // host[:port][/base]
    std::string hostport = s;
    std::string path = "/";
    const size_t slash = s.find('/');
    if (slash != std::string::npos) {
        hostport = s.substr(0, slash);
        path = s.substr(slash);
        if (path.empty()) path = "/";
    }

    std::string host = hostport;
    std::string port = "80";
    const size_t colon = hostport.rfind(':');
    if (colon != std::string::npos && colon + 1 < hostport.size()) {
        host = hostport.substr(0, colon);
        port = hostport.substr(colon + 1);
    }
    if (host.empty()) return p;

    p.host = host;
    p.port = port;
    p.base_path = path;
    if (p.base_path.back() == '/') p.base_path.pop_back();
    p.ok = true;
    return p;
}

static bool starts_with(const std::string& s, const char* lit)
{
    const size_t n = std::strlen(lit);
    if (s.size() < n) return false;
    return std::memcmp(s.data(), lit, n) == 0;
}

static bool is_auto_base_url(const char* base_url)
{
    if (!base_url) return true;
    if (!base_url[0]) return true;
    std::string s(base_url);
    while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(s.begin());
    while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return (s == "auto");
}

static std::string read_wsl2_nameserver_ip()
{
#ifdef _WIN32
    return {};
#else
    std::ifstream f("/etc/resolv.conf");
    if (!f.is_open()) return {};
    std::string line;
    while (std::getline(f, line)) {
        // nameserver 172.xx.xx.xx
        const std::string k = "nameserver";
        auto s = line;
        while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(s.begin());
        if (s.rfind(k, 0) != 0) continue;
        s.erase(0, k.size());
        while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(s.begin());
        if (s.empty()) continue;
        // 截取到空白
        size_t end = 0;
        while (end < s.size() && !std::isspace((unsigned char)s[end])) ++end;
        const std::string ip = s.substr(0, end);
        // 简单校验 ipv4
        in_addr a{};
        if (inet_pton(AF_INET, ip.c_str(), &a) == 1) return ip;
    }
    return {};
#endif
}

static std::string http_decode_chunked(const std::string& body)
{
    // 简易 chunked 解码：容错为主（失败则回退原 body）
    size_t i = 0;
    std::string out;
    out.reserve(body.size());
    while (i < body.size()) {
        // 读 hex 长度行
        size_t line_end = body.find("\r\n", i);
        if (line_end == std::string::npos) return body;
        const std::string len_hex = body.substr(i, line_end - i);
        char* endp = nullptr;
        const long len = std::strtol(len_hex.c_str(), &endp, 16);
        if (endp == len_hex.c_str() || len < 0) return body;
        i = line_end + 2;
        if (len == 0) break;
        if (i + (size_t)len > body.size()) return body;
        out.append(body.data() + i, (size_t)len);
        i += (size_t)len;
        if (i + 2 > body.size()) break;
        if (body.compare(i, 2, "\r\n") == 0) i += 2;
    }
    return out;
}

struct HttpResp {
    int code = -1;
    std::string body;
};

static HttpResp http_get_raw(const std::string& host, const std::string& port, const std::string& path, const std::string& api_key)
{
    HttpResp r;

#ifdef _WIN32
    static std::atomic<bool> wsa_inited{false};
    if (!wsa_inited.exchange(true)) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            return r;
        }
    }
#endif

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0 || !res) {
        return r;
    }

    int sock = -1;
    for (addrinfo* it = res; it; it = it->ai_next) {
        sock = (int)::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (sock < 0) continue;
        if (::connect(sock, it->ai_addr, (int)it->ai_addrlen) == 0) break;
#ifdef _WIN32
        ::closesocket(sock);
#else
        ::close(sock);
#endif
        sock = -1;
    }
    freeaddrinfo(res);
    if (sock < 0) return r;

    std::string req;
    req.reserve(512);
    req += "GET " + path + " HTTP/1.1\r\n";
    req += "Host: " + host + "\r\n";
    req += "User-Agent: lv_port_pc_vscode/mrker\r\n";
    req += "Accept: application/json\r\n";
    req += "Connection: close\r\n";
    if (!api_key.empty()) {
        req += "X-Api-Key: " + api_key + "\r\n";
    }
    req += "\r\n";

    const char* p = req.c_str();
    size_t left = req.size();
    while (left) {
#ifdef _WIN32
        const int n = ::send(sock, p, (int)left, 0);
#else
        const ssize_t n = ::send(sock, p, left, 0);
#endif
        if (n <= 0) {
#ifdef _WIN32
            ::closesocket(sock);
#else
            ::close(sock);
#endif
            return r;
        }
        p += (size_t)n;
        left -= (size_t)n;
    }

    std::string raw;
    raw.reserve(4096);
    char buf[4096];
    while (true) {
#ifdef _WIN32
        const int n = ::recv(sock, buf, (int)sizeof(buf), 0);
#else
        const ssize_t n = ::recv(sock, buf, sizeof(buf), 0);
#endif
        if (n <= 0) break;
        raw.append(buf, buf + n);
        if (raw.size() > 2 * 1024 * 1024) break; // 2MB 上限防爆内存
    }

#ifdef _WIN32
    ::closesocket(sock);
#else
    ::close(sock);
#endif

    const size_t hdr_end = raw.find("\r\n\r\n");
    if (hdr_end == std::string::npos) return r;
    const std::string hdr = raw.substr(0, hdr_end);
    std::string body = raw.substr(hdr_end + 4);

    // parse status
    {
        const size_t sp1 = hdr.find(' ');
        if (sp1 != std::string::npos) {
            const size_t sp2 = hdr.find(' ', sp1 + 1);
            const std::string code_s = hdr.substr(sp1 + 1, sp2 == std::string::npos ? std::string::npos : (sp2 - (sp1 + 1)));
            r.code = std::atoi(code_s.c_str());
        }
    }

    // chunked?
    {
        std::string hdr_l = hdr;
        std::transform(hdr_l.begin(), hdr_l.end(), hdr_l.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        if (hdr_l.find("transfer-encoding: chunked") != std::string::npos) {
            body = http_decode_chunked(body);
        }
    }

    r.body = std::move(body);
    return r;
}

static bool moonraker_probe(const std::string& base_url, const std::string& api_key)
{
    const UrlParts up = parse_http_base_url(base_url);
    if (!up.ok) return false;
    // Moonraker 基本信息接口：成功一般为 200 且返回 JSON
    std::string path = up.base_path;
    path += "/server/info";
    HttpResp hr = http_get_raw(up.host, up.port, path, api_key);
    if (hr.code != 200) return false;
    if (hr.body.find("\"result\"") == std::string::npos) return false;
    return true;
}

static std::string moonraker_auto_discover_base_url(const std::string& api_key)
{
    // 候选顺序：本机 -> WSL2 宿主机 -> 常见 mDNS
    std::vector<std::string> hosts;
    hosts.emplace_back("127.0.0.1");
    if (std::string ip = read_wsl2_nameserver_ip(); !ip.empty()) hosts.push_back(std::move(ip));
    hosts.emplace_back("klipper.local");
    hosts.emplace_back("mainsailos.local");
    hosts.emplace_back("fluidd.local");

    const std::vector<int> ports = {7125, 80};

    for (const auto& h : hosts) {
        for (const int port : ports) {
            std::string base = "http://" + h + ":" + std::to_string(port);
            if (moonraker_probe(base, api_key)) return base;
        }
    }
    return {}; // 未找到
}

// --------- 极简 JSON 提取（为 Moonraker 特定结构服务，不做通用 JSON） ----------

static size_t skip_ws(const std::string& s, size_t i)
{
    while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
    return i;
}

static bool json_extract_object_span_after_key(const std::string& json, const char* key_lit, size_t& out_begin, size_t& out_end)
{
    // 找到 `"key"` : { ... } 的 { ... } span（不含外层空白）
    const std::string key = std::string("\"") + key_lit + "\"";
    size_t pos = json.find(key);
    if (pos == std::string::npos) return false;
    pos += key.size();
    pos = skip_ws(json, pos);
    if (pos >= json.size() || json[pos] != ':') return false;
    pos = skip_ws(json, pos + 1);
    if (pos >= json.size() || json[pos] != '{') return false;

    size_t i = pos;
    int depth = 0;
    bool in_str = false;
    for (; i < json.size(); ++i) {
        const char c = json[i];
        if (in_str) {
            if (c == '\\') {
                ++i; // skip escaped
                continue;
            }
            if (c == '"') in_str = false;
            continue;
        }
        if (c == '"') { in_str = true; continue; }
        if (c == '{') { ++depth; continue; }
        if (c == '}') {
            --depth;
            if (depth == 0) {
                out_begin = pos;
                out_end = i + 1;
                return true;
            }
        }
    }
    return false;
}

static bool json_find_number_after_key(const std::string& json, const char* key_lit, double& out)
{
    const std::string key = std::string("\"") + key_lit + "\"";
    size_t pos = json.find(key);
    if (pos == std::string::npos) return false;
    pos += key.size();
    pos = skip_ws(json, pos);
    if (pos >= json.size() || json[pos] != ':') return false;
    pos = skip_ws(json, pos + 1);
    if (pos >= json.size()) return false;
    // Moonraker 数字可能是整数或浮点；也可能是 null
    if (starts_with(json.substr(pos), "null")) return false;
    size_t end = pos;
    while (end < json.size()) {
        const char c = json[end];
        if (!(std::isdigit((unsigned char)c) || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E')) break;
        ++end;
    }
    if (end == pos) return false;
    out = std::strtod(json.c_str() + pos, nullptr);
    return true;
}

static bool json_find_string_after_key(const std::string& json, const char* key_lit, std::string& out)
{
    out.clear();
    const std::string key = std::string("\"") + key_lit + "\"";
    size_t pos = json.find(key);
    if (pos == std::string::npos) return false;
    pos += key.size();
    pos = skip_ws(json, pos);
    if (pos >= json.size() || json[pos] != ':') return false;
    pos = skip_ws(json, pos + 1);
    if (pos >= json.size()) return false;
    if (starts_with(json.substr(pos), "null")) return false;
    if (json[pos] != '"') return false;
    ++pos;
    std::string s;
    for (size_t i = pos; i < json.size(); ++i) {
        const char c = json[i];
        if (c == '\\') {
            if (i + 1 >= json.size()) break;
            const char n = json[i + 1];
            switch (n) {
            case '"': s.push_back('"'); break;
            case '\\': s.push_back('\\'); break;
            case '/': s.push_back('/'); break;
            case 'b': s.push_back('\b'); break;
            case 'f': s.push_back('\f'); break;
            case 'n': s.push_back('\n'); break;
            case 'r': s.push_back('\r'); break;
            case 't': s.push_back('\t'); break;
            default:
                // \uXXXX 暂不展开（Moonraker 常用 ASCII），保守跳过
                break;
            }
            i += 1;
            continue;
        }
        if (c == '"') {
            out = std::move(s);
            return true;
        }
        s.push_back(c);
    }
    return false;
}

static bool moonraker_parse_status(const std::string& body, MrkerCache& out)
{
    // 预期结构：{"result":{"status":{...}},"eventtime":...}
    // 我们只抓常用字段；缺失则保持 NAN/-1/空串

    size_t result_b = 0, result_e = 0;
    if (!json_extract_object_span_after_key(body, "result", result_b, result_e)) return false;
    const std::string result = body.substr(result_b, result_e - result_b);

    size_t status_b = 0, status_e = 0;
    if (!json_extract_object_span_after_key(result, "status", status_b, status_e)) return false;
    const std::string status = result.substr(status_b, status_e - status_b);

    // extruder
    {
        size_t b = 0, e = 0;
        if (json_extract_object_span_after_key(status, "extruder", b, e)) {
            const std::string o = status.substr(b, e - b);
            double v = 0;
            if (json_find_number_after_key(o, "temperature", v)) out.extruder_temp = (float)v;
            if (json_find_number_after_key(o, "target", v)) out.extruder_target = (float)v;
        }
    }
    // heater_bed
    {
        size_t b = 0, e = 0;
        if (json_extract_object_span_after_key(status, "heater_bed", b, e)) {
            const std::string o = status.substr(b, e - b);
            double v = 0;
            if (json_find_number_after_key(o, "temperature", v)) out.bed_temp = (float)v;
            if (json_find_number_after_key(o, "target", v)) out.bed_target = (float)v;
        }
    }
    // virtual_sdcard.progress 0..1
    {
        size_t b = 0, e = 0;
        if (json_extract_object_span_after_key(status, "virtual_sdcard", b, e)) {
            const std::string o = status.substr(b, e - b);
            double v = 0;
            if (json_find_number_after_key(o, "progress", v)) out.progress01 = (float)v;
        }
    }
    // print_stats.state + filename
    {
        size_t b = 0, e = 0;
        if (json_extract_object_span_after_key(status, "print_stats", b, e)) {
            const std::string o = status.substr(b, e - b);
            std::string s;
            if (json_find_string_after_key(o, "state", s)) strlcpy0(out.state, sizeof(out.state), s);
            if (json_find_string_after_key(o, "filename", s)) strlcpy0(out.filename, sizeof(out.filename), s);
        }
    }
    // display_status.message
    {
        size_t b = 0, e = 0;
        if (json_extract_object_span_after_key(status, "display_status", b, e)) {
            const std::string o = status.substr(b, e - b);
            std::string s;
            if (json_find_string_after_key(o, "message", s)) strlcpy0(out.message, sizeof(out.message), s);
        }
    }

    return true;
}

static void poll_once()
{
    const UrlParts up = parse_http_base_url(g_base_url);
    if (!up.ok) {
        std::lock_guard<std::mutex> lk(g_mu);
        g_cache.connected = false;
        g_cache.http_code = -1;
        return;
    }

    // 建议轮询关键字段：挤出头/热床/虚拟 SD/打印状态/屏显
    std::string path = up.base_path;
    path += "/printer/objects/query?extruder=temperature,target&heater_bed=temperature,target&virtual_sdcard=progress&print_stats=state,filename&display_status=message";

    HttpResp hr = http_get_raw(up.host, up.port, path, g_api_key);

    MrkerCache next;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        next = g_cache; // 保留已有值做增量覆盖
    }

    next.http_code = hr.code;
    if (hr.code != 200 || hr.body.empty()) {
        next.connected = false;
        std::lock_guard<std::mutex> lk(g_mu);
        g_cache = next;
        return;
    }

    MrkerCache parsed = next;
    if (!moonraker_parse_status(hr.body, parsed)) {
        parsed.connected = false;
        std::lock_guard<std::mutex> lk(g_mu);
        g_cache = parsed;
        return;
    }

    parsed.connected = true;
    parsed.last_ok_tick = lv_tick_get();
    std::lock_guard<std::mutex> lk(g_mu);
    g_cache = parsed;
}

static void thread_main()
{
    // 500ms 轮询；失败退避到 1500ms
    uint32_t fail_n = 0;
    while (g_running.load(std::memory_order_relaxed)) {
        const uint32_t t0 = lv_tick_get();
        poll_once();
        bool ok = false;
        {
            std::lock_guard<std::mutex> lk(g_mu);
            ok = g_cache.connected;
        }
        if (ok) fail_n = 0;
        else fail_n = std::min<uint32_t>(fail_n + 1, 10);

        const uint32_t base_ms = 500;
        const uint32_t backoff_ms = 1500;
        const uint32_t period = (fail_n >= 2) ? backoff_ms : base_ms;

        const uint32_t used = lv_tick_elaps(t0);
        const uint32_t sleep_ms = (used >= period) ? 1u : (period - used);
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    }
}

} // namespace

extern "C" {

void mrker_configure(const char* base_url, const char* api_key)
{
    if (api_key && api_key[0]) g_api_key = api_key;
    else g_api_key.clear();

    if (is_auto_base_url(base_url)) {
        if (std::string found = moonraker_auto_discover_base_url(g_api_key); !found.empty()) {
            g_base_url = std::move(found);
        } else {
            // 回退默认
            g_base_url = "http://127.0.0.1:7125";
        }
        std::lock_guard<std::mutex> lk(g_mu);
        strlcpy0(g_base_url_cstr, sizeof(g_base_url_cstr), g_base_url);
        return;
    }

    g_base_url = base_url;
    std::lock_guard<std::mutex> lk(g_mu);
    strlcpy0(g_base_url_cstr, sizeof(g_base_url_cstr), g_base_url);
}

const char* mrker_base_url(void)
{
    std::lock_guard<std::mutex> lk(g_mu);
    // 避免把 std::string::c_str() 暴露到锁外导致竞态
    strlcpy0(g_base_url_cstr, sizeof(g_base_url_cstr), g_base_url);
    return g_base_url_cstr;
}

void mrker_start(void)
{
    bool expected = false;
    if (!g_running.compare_exchange_strong(expected, true)) return;
    g_thr = std::thread(thread_main);
}

void mrker_stop(void)
{
    if (!g_running.exchange(false)) return;
    if (g_thr.joinable()) g_thr.join();
}

bool mrker_is_connected(void)
{
    std::lock_guard<std::mutex> lk(g_mu);
    return g_cache.connected;
}

int mrker_last_http_code(void)
{
    std::lock_guard<std::mutex> lk(g_mu);
    return g_cache.http_code;
}

uint32_t mrker_last_ok_tick(void)
{
    std::lock_guard<std::mutex> lk(g_mu);
    return g_cache.last_ok_tick;
}

float mrker_print_progress01(void)
{
    std::lock_guard<std::mutex> lk(g_mu);
    return g_cache.progress01;
}

const char* mrker_print_state(void)
{
    std::lock_guard<std::mutex> lk(g_mu);
    return g_cache.state;
}

const char* mrker_print_filename(void)
{
    std::lock_guard<std::mutex> lk(g_mu);
    return g_cache.filename;
}

const char* mrker_display_message(void)
{
    std::lock_guard<std::mutex> lk(g_mu);
    return g_cache.message;
}

float mrker_extruder_temp(void)
{
    std::lock_guard<std::mutex> lk(g_mu);
    return g_cache.extruder_temp;
}

float mrker_extruder_target(void)
{
    std::lock_guard<std::mutex> lk(g_mu);
    return g_cache.extruder_target;
}

float mrker_bed_temp(void)
{
    std::lock_guard<std::mutex> lk(g_mu);
    return g_cache.bed_temp;
}

float mrker_bed_target(void)
{
    std::lock_guard<std::mutex> lk(g_mu);
    return g_cache.bed_target;
}

} // extern "C"

