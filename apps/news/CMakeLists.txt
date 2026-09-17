# apps/news/CMakeLists.txt
#
# 数据源二选一，controller / view 两边通用，不需要改动：
#   mock（默认）  service/news_svc_mock.c   本地假数据，不需要服务端
#   http          service/news_svc_http.c   连 server/news 的 HTTP 接口
#
# 切换：idf.py -DNEWS_SVC=http build
# NEWS_SVC 是 CMake cache 变量，会一直留在 build/ 里，换回去要显式写
#   idf.py -DNEWS_SVC=mock build
# 服务端地址写在 service/news_svc_http.c 顶部的 NEWS_API_BASE。

if(NOT DEFINED NEWS_SVC)
    set(NEWS_SVC "mock")
endif()

if(NEWS_SVC STREQUAL "http")
    set(NEWS_SVC_SRC "service/news_svc_http.c")
elseif(NEWS_SVC STREQUAL "mock")
    set(NEWS_SVC_SRC "service/news_svc_mock.c")
else()
    message(FATAL_ERROR "NEWS_SVC must be 'mock' or 'http', got '${NEWS_SVC}'")
endif()
message(STATUS "news app data source: ${NEWS_SVC}")

idf_component_register(
    SRCS
        "app.c"
        "model.c"
        "view.c"
        "controller.c"
        "${NEWS_SVC_SRC}"
        "subpages/view_launch.c"
        "subpages/view_list.c"
        "subpages/view_detail.c"
        "subpages/controller_list.c"
        "subpages/controller_detail.c"
    INCLUDE_DIRS "." "subpages" "service"
    REQUIRES lvgl
    # json / esp_http_client / wifi 只有 http 实现用得上，但这里不能跟着
    # NEWS_SVC 分支写：ESP-IDF 先用 script mode 跑一遍本文件收集依赖，那一趟
    # 拿不到命令行传进来的 -DNEWS_SVC，会按默认的 mock 分支算出空依赖，
    # 真正编译时 news_svc_http.c 就找不到 esp_http_client.h。
    # 依赖多列几个不影响产物（这几个组件本来就被 ota / chatbot 拉进来了）。
    PRIV_REQUIRES appmgr uilv json esp_http_client wifi
)
