#pragma once
#include "../eventbuf.h"

/* Инициализация TUI: создаёт окна ncurses, запускает цикл отрисовки.
   Блокирует вызывающий поток до выхода пользователя (q / Ctrl+C). */
void gui_run(EventBuf *events, const char *prog_name);

/* Завершение: восстанавливает терминал. Вызывается автоматически внутри gui_run. */
void gui_shutdown(void);
