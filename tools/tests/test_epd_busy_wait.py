# Host regression for real controller BUSY wait code, using virtual RTOS time.
# Run: python tools/tests/test_epd_busy_wait.py [--baseline]
import pathlib
import subprocess
import sys
import tempfile
root = pathlib.Path(__file__).resolve().parents[2]
driver = (root / 'drivers/gdem0397t81p/Display_EPD_W21.c').read_text(encoding='utf-8')
controller = (root / 'system/controller/display_control.c').read_text(encoding='utf-8')
if '--baseline' in sys.argv:
    controller = subprocess.check_output(['git', 'show', 'HEAD:system/controller/display_control.c'], cwd=root, text=True, encoding='utf-8')
def function(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]
# Compile production functions and call sites, replacing only hardware/time.
before = controller.split('if (!s_display.display) continue;', 1)[1].split('/* Take an immutable frame', 1)[0]
after = controller.split('if (transition_frame) {', 2)[2].split('/* The physical panel', 1)[0]
harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define EPD_PIN_BUSY 21
#define EPD_PIN_MOSI 10
#define EPD_PIN_SCK 11
#define EPD_PIN_CS 12
#define EPD_BUSY_TIMEOUT_MS 10000
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define pdMS_TO_TICKS(ms) (ms)
static unsigned elapsed, release_at;
static int gpio_get_level(int pin) { (void)pin; return elapsed < release_at; }
static void vTaskDelay(unsigned ms) { elapsed += ms; assert(elapsed <= 10010); }
''' + '\n'.join(function(driver, signature) for signature in [
    'static void delay_xms(', 'bool epd_is_busy(', 'bool epd_wait_ready('])
harness += '\nstatic void before_frame(void) {' + before + '}\n'
harness += '\nstatic void after_transition(void) {' + after + '}\n'
harness += r'''
int main(void) {
    elapsed = release_at = 0; before_frame(); assert(elapsed == 0);
    elapsed = 0; release_at = 200; before_frame(); assert(elapsed == 200);
    elapsed = 0; release_at = UINT32_MAX; before_frame(); assert(elapsed == 10000);
    elapsed = 0; after_transition(); assert(elapsed == 10000);
    elapsed = 0; assert(!epd_wait_ready());
    elapsed = release_at = 0; assert(epd_wait_ready());
    puts("PASS: stuck BUSY cannot block frame submission or transition completion forever");
}
'''
with tempfile.TemporaryDirectory(prefix='deepstoa-busy-') as temp:
    source = pathlib.Path(temp) / 'test.c'
    exe = pathlib.Path(temp) / 'test.exe'
    source.write_text(harness, encoding='utf-8')
    subprocess.run(['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(source), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=5)
