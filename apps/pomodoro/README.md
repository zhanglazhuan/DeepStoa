# Pomodoro

The active MVC application uses a shared LVGL
panel under `system/uilv/widgets` renders the 25-minute timer and controls.
`system/services/pomodoro_service` owns monotonic timing, so closing the page does not
stop a running session. Completion uses a separate global event and alert,
without changing the Clock application's countdown timer. Reopening the app
shows the current state; reset explicitly starts a fresh session.

The original hardware page is archived under `archive/lxj_pomodoro/legacy`.
