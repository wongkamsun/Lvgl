/**
 * @file main.cpp
 * 适配 WSL + C++ + Python 预编译 UI 逻辑
 */

 #include <iostream>
 #include <cstdio>
 #include <cstdlib>
 #include <vector>
 #include <filesystem>

 // 包含标准库
 #ifndef _DEFAULT_SOURCE
   #define _DEFAULT_SOURCE
 #endif

 #include <stdlib.h>
 #include <stdio.h>

 #ifdef _MSC_VER
   #include <Windows.h>
 #else
   #include <unistd.h>
 #endif

// 包含 LVGL
 #include "lvgl/lvgl.h"
 #include <chrono>

 // 模拟器驱动 (保持你原来的 hal.h 路径)
 #include "hal/hal.h"
 #include "widgets_bin.h"
 #include "widget.h"
 #include "win.h"
 #include "../app/main_win.h"
 #include "../app/set_window.h"

 int main(int argc, char **argv)
 {
     const std::filesystem::path exe_path = (argc >= 1 && argv && argv[0]) ? std::filesystem::path(argv[0]) : std::filesystem::path{};
     std::filesystem::path proj_root;
     try {
         // bin/main -> project root
         proj_root = std::filesystem::weakly_canonical(exe_path).parent_path().parent_path();
     } catch (...) {
         proj_root.clear();
     }

     /* 1. 初始化 LVGL 核心 */
     lv_init();

     /* 2. 初始化显示和输入设备 */
     /* 按照你的要求设置为 800x480 分辨率 */
     sdl_hal_init(1080, 720);

     // 窗口管理：一个 bin 文件对应一个界面
     WinManager win;
     {
         // 图片目录：ui/resources/images
         {
             const auto img_dir = proj_root.empty() ? std::filesystem::path("ui/resources/images") : (proj_root / "ui/resources/images");
             std::string im_err;
             if (!widgets_images_init(img_dir.string().c_str(), im_err) && !im_err.empty())
                 std::cerr << "widgets_images_init failed: " << im_err << std::endl;
             else if (std::getenv("WIDGETS_IMG_DEBUG"))
                 widgets_images_log_diagnostics(1);
         }

         std::vector<std::string> bin_paths;
         // 约定 windowIndex 从 1 开始；下标 0 留空   注册对应的界面
         bin_paths.resize(3);
         const auto p1 = proj_root.empty() ? std::filesystem::path("ui/ui_1.bin") : (proj_root / "ui/ui_1.bin");
         const auto p2 = proj_root.empty() ? std::filesystem::path("ui/ui_2.bin") : (proj_root / "ui/ui_2.bin");
         bin_paths[1] = p1.string();
         bin_paths[2] = p2.string();

         std::string err;
         if (!win.init(lv_scr_act(), std::move(bin_paths), err)) {
             std::cerr << "win.init failed: " << err << std::endl;
         } else {
             // 每个窗口绑定各自的翻译表（示例命名：ui/translation_1.bin, ui/translation_2.bin）
             std::vector<std::string> tr_paths;
             tr_paths.resize(3);
             const auto tf = proj_root.empty() ? std::filesystem::path("ui/tr.bin") : (proj_root / "ui/tr.bin");
             const auto t1 = proj_root.empty() ? std::filesystem::path("ui/tr_1.bin") : (proj_root / "ui/tr_1.bin");
             const auto t2 = proj_root.empty() ? std::filesystem::path("ui/tr_2.bin") : (proj_root / "ui/tr_2.bin");
             // 优先使用每窗口的 tr_*.bin；若不存在则回退到 ui/tr.bin；都不存在则留空（不切换翻译）
             if (std::filesystem::exists(t1)) tr_paths[1] = t1.string();
             else if (std::filesystem::exists(tf)) tr_paths[1] = tf.string();
             if (std::filesystem::exists(t2)) tr_paths[2] = t2.string();
             else if (std::filesystem::exists(tf)) tr_paths[2] = tf.string();
             std::string tr_err;
             if (!win.set_translation_bins(std::move(tr_paths), tr_err) && !tr_err.empty())
                 std::cerr << "set_translation_bins failed: " << tr_err << std::endl;
             win.set_translation_language(1); // 默认语言：0=中文，1=English...

            //界面注册
             std::string e1;
             if (!widgets_register_window_common(win, 1, app_main_win_register, &win, e1) && !e1.empty())
                 std::cerr << "register window1 failed: " << e1 << std::endl;
             std::string e2;
             if (!widgets_register_window_common(win, 2, app_set_window_register, &win, e2) && !e2.empty())
                 std::cerr << "register window2 failed: " << e2 << std::endl;

             err.clear();
             if (!win.switch_to(1, err)) {
                 std::cerr << "win.switch_to(1) failed: " << err << std::endl;
             }
         }
     }

     std::cout << "LVGL C++ Simulator Started [Resolution: 800x480]" << std::endl;

     /* 主循环 */
     auto last_tp = std::chrono::steady_clock::now();
     while(1) {
         auto now_tp = std::chrono::steady_clock::now();
         uint32_t elaps_ms = (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(now_tp - last_tp).count();
         if(elaps_ms) {
             lv_tick_inc(elaps_ms);
             last_tp = now_tp;
         }

         /* 处理 LVGL 任务（动画、渲染、事件） */
         uint32_t sleep_time_ms = lv_timer_handler();

         if(sleep_time_ms == LV_NO_TIMER_READY){
            sleep_time_ms = LV_DISP_DEF_REFR_PERIOD;
         }

 #ifdef _MSC_VER
         Sleep(sleep_time_ms);
 #else
         // usleep 单位是微秒，所以乘以 1000
         usleep(sleep_time_ms * 1000);
 #endif
     }

     return 0;
 }
