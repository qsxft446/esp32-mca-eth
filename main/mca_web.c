#include "mca_web.h"
#include "mca_dsp.h"
#include "mca_prof.h"
#include "mca_settings.h"
#include "adc_cap.h"
#include "adc_clk.h"
#include "mca_diag.h"
#include "mca_eth.h"
#include "mca_emu.h"
#include "scope_codec.h"
#include "mca_hist.h"
#include "mca_dose.h"
#include "mca_time.h"
#include "mca_ota.h"
#include "mca_oled.h"
#include "mca_log.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <inttypes.h>
#include <math.h>
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "mca_web";

volatile mca_mode_t mca_mode        = MCA_MODE_SPECTRUM;
volatile bool       mca_spec_run    = false; /* до «Старт» набора нет  */
volatile bool       mca_scope_run   = true;  /* открыли - сразу видно  */
volatile bool       mca_cmd_clear   = false;
volatile int        mca_cmd_freq_idx= -1;

/* ---------------- страница ---------------- */
/* Общий стиль всех страниц:
 * тёмная зелёная гамма, моноширинный шрифт, панели, цветные кнопки
 * действий, сегментные переключатели. Отдаётся одним файлом /s.css,
 * а не копией в каждой странице. */
static const char CSS[] =
":root{--accent:#34d3c0;--accent-dim:rgba(52,211,192,.12);--bg:#0a0c0a;"
"--panel:#11150f;--bd:#20281e;--dim:#8a9686;--txt:#cfd6cd}"
"*{box-sizing:border-box}"
"html,body{margin:0;padding:0;background:var(--bg)}"
"body{color:var(--txt);font-size:13px;padding-bottom:28px;"
"font-family:ui-monospace,'SF Mono','Cascadia Mono','DejaVu Sans Mono',Consolas,monospace}"
"a{color:var(--accent);text-decoration:none}"
"header{display:flex;align-items:center;justify-content:space-between;gap:16px;"
"padding:8px 24px;border-bottom:1px solid #1c241c}"
".brand{display:flex;align-items:center;gap:13px;font-weight:700;font-size:16px;color:#e8efe2}"
".brand img{height:56px;width:auto;display:block}"
".chips{display:flex;align-items:center;gap:8px;flex-wrap:wrap;justify-content:flex-end}"
".chip{background:#10140f;border:1px solid #243024;border-radius:999px;padding:5px 11px;"
"font-size:12px;color:var(--dim);display:inline-flex;align-items:center;gap:7px}"
".chip.warn{color:#f0c45a;border-color:rgba(240,180,60,.35)}"
".dot{width:7px;height:7px;border-radius:50%;background:var(--accent);box-shadow:0 0 7px var(--accent)}"
".dot.off{background:#5b655a;box-shadow:none}"
"nav{display:flex;align-items:center;gap:6px;padding:0 20px;border-bottom:1px solid #1c241c;"
"overflow-x:auto;scrollbar-width:none}"
/* на узком экране вкладки листаются пальцем, а системная полоса
   прокрутки белым пятном ломала тёмную шапку */
"nav::-webkit-scrollbar{display:none}"
"nav a,nav span{padding:12px 16px;font-size:13px;color:var(--dim);flex-shrink:0;white-space:nowrap;cursor:pointer}"
"nav .on{font-weight:600;color:#e8efe2;border-bottom:2px solid var(--accent)}"
".wrap{padding:18px 22px 0;display:flex;flex-direction:column;gap:14px}"
".panel{background:var(--panel);border:1px solid var(--bd);border-radius:10px}"
".pad{padding:13px 16px}"
".row{display:flex;align-items:center;gap:16px;padding:12px 16px;flex-wrap:wrap}"
".row+.row{border-top:1px solid #1b211a}"
".grp{display:flex;align-items:center;gap:8px;flex-wrap:wrap}"
".status{display:flex;align-items:center;gap:14px;flex-wrap:wrap;font-size:12.5px;color:var(--dim);padding:2px}"
".sep{color:#3a443a}.ac{color:var(--accent);font-weight:600}.w{color:var(--txt)}.mut{color:#6b756a}"
"button{border-radius:8px;padding:9px 16px;font-size:13px;cursor:pointer;background:#161b14;"
"border:1px solid #2a322a;color:var(--txt);font-family:inherit}"
".btn.sm,button.sm{padding:6px 12px;font-size:12px}"
".btn.green{background:var(--accent-dim);border-color:rgba(52,211,192,.5);color:var(--accent);font-weight:600}"
".btn.amber{background:rgba(240,180,60,.08);border-color:rgba(240,180,60,.45);color:#f0c45a}"
".btn.red{background:rgba(240,90,80,.07);border-color:rgba(240,90,80,.45);color:#ff8a82}"
"button:hover:not(:disabled){filter:brightness(1.2)}"
"button:disabled{opacity:.4;cursor:default}"
".seg{display:flex;border:1px solid #2a322a;border-radius:7px;overflow:hidden}"
".seg button{padding:7px 14px;font-size:12px;border:none;border-radius:0;background:transparent;color:#9aa496}"
".seg button.on{background:var(--accent);color:#0c1206;font-weight:700}"
".lbl,label{font-size:11.5px;color:#6b756a}"
"input,select{background:#0c0f0b;border:1px solid #283024;color:#e8efe2;padding:6px 9px;"
"border-radius:7px;font-size:12px;font-family:inherit;outline:none}"
"input[type=number]{width:78px}"
"input:focus,select:focus{border-color:rgba(52,211,192,.5)}"
"input[type=checkbox]{accent-color:var(--accent);vertical-align:middle}"
".big{font-size:26px;font-weight:600;color:#e8efe2;letter-spacing:1px}"
".big.ac{color:var(--accent);font-size:22px}.big.w{font-size:22px}"
".ml{margin-left:auto}"
"canvas{width:100%;display:block;background:#0c0f0b;border-radius:6px}"
".mono{font-size:12px;line-height:1.7;color:#aeb6a8}"
"table{border-collapse:collapse;width:100%;font-size:12px;font-variant-numeric:tabular-nums}"
/* «параметр - значение»: ширина колонок не зависит от содержимого - иначе
   значения при обновлении ездят влево-вправо, а у соседних таблиц не
   совпадают */
"table.kv{table-layout:fixed}table.kv tr>:first-child{width:38%}"
"table.kv td{overflow-wrap:anywhere}"
"th,td{border-bottom:1px solid #1b211a;padding:7px 10px;text-align:left;color:var(--txt);vertical-align:top}"
"th{color:var(--accent);font-weight:600;font-size:11.5px}"
"h3{margin:0 0 8px;font-size:15px;color:#e8efe2}"
"h4{margin:14px 0 4px;color:var(--accent);font-size:13px}"
"code{background:#0c0f0b;border:1px solid #20281e;padding:1px 5px;border-radius:4px;font-size:11.5px;color:#aeb6a8}"
".ok{color:var(--accent)}.bad{color:#ff8a82}.warn{color:#f0c45a}"
".n{color:#6b756a;font-size:12px;line-height:1.6}"
/* таблица настроек на вкладке осциллографа */
/* separate, а не collapse: половинки схлопнутых границ дают лишний
   пиксель ширины и полосу прокрутки под таблицей */
".ptab{width:100%;border-collapse:separate;border-spacing:0;font-size:12.5px}"
".ptab td{padding:5px 8px;border-bottom:1px solid #1f261f;vertical-align:middle}"
".ptab tr.gh td{padding-top:14px;color:#e8efe2;font-weight:600;border-bottom:1px solid #2c362c}"
".ptab td:first-child{white-space:nowrap}"
".ptab td:nth-child(2),.ptab td:nth-child(3){width:1%;white-space:nowrap}"
".ptab td:last-child{color:#8a948a;min-width:220px}"
".ptab label{border-bottom:1px dotted #3a443a;cursor:help}"
".ptab input,.ptab select{width:104px}"
".ptab tr.off{opacity:.35}"
".bdg{display:inline-block;padding:1px 8px;border-radius:9px;font-size:11px;"
"border:1px solid #3a443a;color:#9aa59a;white-space:nowrap}"
".bdg.t{color:#f0c45a;border-color:#6b5a2a}.bdg.i{color:#34d3c0;border-color:#1f5f57}"
/* переключатель языка в шапке и блоки справки на двух языках (см. LJS) */
".lng button{padding:4px 9px;font-size:11px}"
"html[lang=en] .lr,html[lang=ru] .le{display:none}"
/* мониторинг CPS на вкладке «Спектр» */
".mcards{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:10px}"
".mcard{border:1px solid var(--bd);border-radius:8px;padding:10px 12px}"
".mcard .big{margin:4px 0}.mcard .mut{font-size:11.5px}"
".mtab{max-height:260px;overflow-y:auto;margin-top:10px}"
"@media (max-width:620px){.big{font-size:20px}.big.ac,.big.w{font-size:18px}"
"header{padding:8px 12px}.wrap{padding:12px 10px 0}.brand img{height:44px}}";

/* ЯЗЫК СТРАНИЦ: русский и английский. Один файл /l.js на все страницы,
 * как стиль, подключается в <head> до разметки.
 * Язык - выбор в этом браузере (кнопки RU/EN в шапке), а если выбора не
 * было - язык браузера. Смена языка перезагружает страницу.
 * Тексты - парами прямо на месте:
 *   - в разметке у элемента атрибут data-en: при английском его
 *     содержимое заменяется на английское; title - атрибут data-en-title;
 *   - в скриптах L('русский','english');
 *   - длинная справка - два блока, class=lr и class=le (прячет стиль).
 * Новые надписи - сразу парой; проверка: python tools/check_i18n.py. */
static const char LJS[] =
"var LANG=(function(){try{var s=localStorage.getItem('mca_lang');if(s=='ru'||s=='en')return s}catch(e){}"
"return /^ru/i.test(navigator.language||'')?'ru':'en'})();"
"document.documentElement.lang=LANG;"
"function L(r,e){return LANG=='en'?e:r}"
"function setLang(l){if(l==LANG)return;try{localStorage.setItem('mca_lang',l)}catch(e){}location.reload()}"
/* Страницы зовут i18n() первой строкой своего скрипта, чтобы их код уже
   видел переведённую разметку; повторный вызов ничего не делает. */
"function i18n(){var a,i;"
"if(LANG=='en'){a=document.querySelectorAll('[data-en]');"
"for(i=0;i<a.length;i++){a[i].innerHTML=a[i].getAttribute('data-en');a[i].removeAttribute('data-en')}"
"a=document.querySelectorAll('[data-en-title]');"
"for(i=0;i<a.length;i++){a[i].title=a[i].getAttribute('data-en-title');a[i].removeAttribute('data-en-title')}}"
"a=document.querySelectorAll('.lng button');"
"for(i=0;i<a.length;i++)a[i].className=a[i].textContent.toLowerCase()==LANG?'on':''}"
"document.addEventListener('DOMContentLoaded',i18n);"
/* ЧАСЫ ПРИБОРА (mca_time.c): своих часов у него нет. Если время не от
   SNTP и не задано или ушло больше чем на 2 с, любая открытая страница
   отдаёт прибору время браузера; повтор раз в час. */
"function tsync(){try{fetch('/time').then(function(r){return r.json()}).then(function(j){"
"if(!j||j.src==1)return;var d=Date.now();if(!j.now||Math.abs(j.now-d)>2000)fetch('/time?set='+d)})"
".catch(function(){})}catch(e){}}"
"document.addEventListener('DOMContentLoaded',function(){tsync();setInterval(tsync,3600000)});";

static const char PAGE[] =
"<!DOCTYPE html><html lang=ru><head><meta charset=utf-8><link rel=icon href=/favicon.ico>"
"<meta name=viewport content='width=device-width,initial-scale=1'>"
"<title>MCA · AD9226</title><link rel=stylesheet href=/s.css><script src=/l.js></script></head><body>"
"<header><div class=brand><img src=/logo.png?v=2 alt=''>MCA "
"<span style=color:var(--accent)>AD9226</span></div>"
"<div class=chips><span class=chip id=runchip><span class='dot off' id=rundot></span>"
"<span id=runtxt data-en='no connection'>нет связи</span></span><span class=chip id=fchip>&mdash;</span>"
/* язык страниц: RU / EN (см. LJS) - рядом с частотой АЦП */
"<span class='seg lng'><button onclick=\"setLang('ru')\">RU</button>"
"<button onclick=\"setLang('en')\">EN</button></span></div></header>"
/* Вкладки - это ВИД страницы (скрытый список md), а не режим прибора:
   открыть осциллограф не значит его запустить. Режим прибора (DM, из
   /stat) меняется только кнопкой «Старт» на вкладке. */
"<nav id=tabs><span data-md=0 onclick=tab(0) data-en='Spectrum'>Спектр</span>"
"<span data-md=1 onclick=tab(SCM) data-en='Oscilloscope'>Осциллограф</span>"
"<a href=/settings data-en='Settings'>Настройки</a><a href=/diag data-en='Diagnostics'>Диагностика</a>"
"<a href=/help data-en='Help'>Справка</a></nav>"
"<select id=md hidden><option value=0><option value=1>"
"<option value=2><option value=3></select>"
"<div class=wrap>"
"<div class=status id=st data-en='connecting...'>подключение...</div>"
"<section class=panel>"
"<div class=row><div class=grp>"
"<button class='btn green' onclick=run(1) data-en='&#9654; Start'>&#9654; Старт</button>"
"<button class='btn amber' onclick=run(0) data-en='&#9632; Stop'>&#9632; Стоп</button>"
"<button class='btn red' id=bclr onclick=cmd('clear') data-en='&#8634; Reset'>&#8634; Сброс</button></div>"
"<div class=ml id=g_big style='display:flex;align-items:center;gap:16px;flex-wrap:wrap'>"
"<span class=big id=bt title='Время набора спектра' data-en-title='Spectrum acquisition time'>00:00:00</span>"
"</div></div>"
"<div class=row>"
"<span class=grp id=g_sp><span class=lbl data-en='Y scale'>Шкала Y</span>"
"<span class=seg><button id=blin onclick=setLog(0)>Lin</button>"
"<button id=blog class=on onclick=setLog(1)>Log</button></span>"
/* ось X: каналы или кэВ (кэВ - только с годной калибровкой) */
"<span class=lbl style=margin-left:8px data-en='X axis'>ось X</span>"
"<span class=seg><button id=cax0 onclick=calAx(0) data-en='channels'>каналы</button>"
"<button id=cax1 onclick=calAx(1) data-en='keV'>кэВ</button></span>"
"<input type=checkbox id=lg checked hidden>"
"<span class=lbl style=margin-left:8px title='Пик можно выделить мышью на графике: щелчок по пику - выделить его, протяжка - свой участок, двойной щелчок - сбросить' data-en-title='A peak can be selected with the mouse on the plot: a click on a peak selects it, a drag - your own range, a double click - reset' data-en='peak from'>пик от</span><input id=pka type=number value=0 onchange=poll()>"
"<span class=lbl data-en='to'>до</span><input id=pkb type=number value=0 onchange=poll()>"
"<span class=lbl style=margin-left:8px title='Сохранить спектр файлом: "
"XML - ResultDataFile (BecqMoni), CSV - канал и счёт, N42 - ANSI N42.42, SPE - SpectraLine. "
"Выгружаются каналы, видимые на экране; время замера берётся из часов компьютера.' "
"data-en-title='Save the spectrum to a file: XML - ResultDataFile (BecqMoni), CSV - channel and count, "
"N42 - ANSI N42.42, SPE - SpectraLine. The channels visible on screen are exported; the measurement "
"time is taken from the computer clock.' data-en='export'>выгрузить</span>"
"<button class='btn sm' onclick=expo('xml')>XML</button>"
"<button class='btn sm' onclick=expo('csv')>CSV</button>"
"<button class='btn sm' onclick=expo('n42')>N42</button>"
"<button class='btn sm' onclick=expo('spe')>SPE</button></span>"
"<span class=grp id=g_sc>"
"<span class=seg><button id=bauto onclick=scm(1) title='синхронизация по фронту, а без фронта - свободный пуск' "
"data-en-title='edge trigger; without an edge - free run' data-en='Auto'>Авто</button>"
"<button id=bwait onclick=scm(3) title='только по фронту, держит последний пойманный импульс' "
"data-en-title='edge only; holds the last captured pulse' data-en='Normal'>Ждущий</button></span>"
"<span class=lbl title='Перепад сигнала в сторону импульса (с учётом полярности) за 8 отсчётов, коды АЦП. Выше шума, но ниже амплитуды нужных импульсов.' "
"data-en-title='Signal step toward the pulse (polarity-aware) over 8 samples, ADC codes. Above the noise but below the height of the wanted pulses.' "
"data-en='trigger on edge &ge;'>синхр. по фронту &ge;</span><input id=tl type=number value=30>"
"<span class=lbl title='Синхронизация по амплитуде: показывать только импульсы, у которых высота над базой (коды АЦП, с учётом полярности) в этом диапазоне. 0 и 0 - без фильтра. Высота показанного импульса и сколько отброшено - под графиком.' "
"data-en-title='Amplitude trigger: show only pulses whose height above the baseline (ADC codes, polarity-aware) is within this range. 0 and 0 - no filter. The height of the shown pulse and how many were rejected are shown under the plot.' "
"data-en='amplitude from'>амплитуда от</span><input id=amin type=number value=0 min=0>"
"<span class=lbl data-en='to'>до</span><input id=amax type=number value=0 min=0>"
"<span class=lbl title='От какой линии отсчитывать ось Y' data-en-title='Which line the Y axis is measured from' data-en='Y axis from'>ось Y от</span>"
"<span class=seg><button id=rf0 onclick=setRef(0) title='от измеренной базовой линии' data-en-title='from the measured baseline' data-en='baseline'>базы</button>"
"<button id=rf1 onclick=setRef(1) title='от середины шкалы АЦП, код 2048; на плате со входом ±5 В это 0 В' "
"data-en-title='from the middle of the ADC scale, code 2048; on a board with a ±5 V input this is 0 V'>2048</button>"
"<button id=rf2 onclick=setRef(2) title='настоящие коды АЦП 0..4095' data-en-title='true ADC codes 0..4095' data-en='codes'>кодов</button></span>"
"<label title='вся шкала АЦП: видно, сколько осталось до потолка и до пола' "
"data-en-title='full ADC scale: shows how much room is left to the ceiling and the floor'>"
"<input type=checkbox id=fx onchange=vis()> <span data-en='full scale'>вся шкала</span></label></span>"
"<span class=grp id=g_zm>"
"<span class=lbl title='Сколько отсчётов на экране. Время на деление подписано на графике.' "
"data-en-title='How many samples are on screen. Time per division is shown on the plot.' data-en='timebase'>развёртка</span>"
"<select id=zm><option value=0 data-en='auto'>авто</option><option value=64>64</option>"
"<option value=128>128</option><option value=256>256</option>"
"<option value=512>512</option><option value=1024>1024</option>"
"<option value=2048>2048</option><option value=4096>4096</option>"
"<option value=8192>8192</option><option value=16384>16384</option>"
"<option value=32768 data-en='32768 smp'>32768 отсч</option></select>"
"<span class=lbl title='Подписи сетки по горизонтали: время или номера отсчётов. Отсчёты считаются от момента синхронизации (синяя метка = 0), до неё - отрицательные: так прямо с экрана читаются числа для L, G, «до/после вершины», перезапуска и поиска пика.' "
"data-en-title='Horizontal grid labels: time or sample numbers. Samples are counted from the trigger point (blue mark = 0), negative before it: this way the numbers for L, G, before/after peak, re-arm and peak search are read straight from the screen.' "
"data-en='X axis'>ось X</span>"
"<span class=seg><button id=bx0 onclick=setX(0) data-en='µs'>мкс</button>"
"<button id=bx1 onclick=setX(1) data-en='smp'>отсч</button></span>"
"<span class=lbl title='Масштаб по вертикали, как на осциллографе: сколько кодов АЦП в одной клетке, клеток 8. База стоит на первой линии снизу (при импульсах вниз - сверху), импульс выше экрана обрезается, у края - метка. Авто - масштаб по сигналу.' "
"data-en-title='Vertical scale, as on an oscilloscope: how many ADC codes per division, 8 divisions. The baseline sits on the first line from the bottom (from the top for downward pulses), a pulse above the screen is clipped, with a mark at the edge. Auto - scale from the signal.' "
"data-en='codes/div'>кодов/клетку</span>"
"<select id=ydiv onchange=ydivSet()><option value=0 data-en='auto'>авто</option><option value=1>1</option>"
"<option value=2>2</option><option value=5>5</option><option value=10>10</option><option value=20>20</option>"
"<option value=50>50</option><option value=100>100</option><option value=200>200</option><option value=500>500</option></select>"
"<button class='btn sm' id=bfix onclick=bfix() title='Запомнить текущий уровень базы и дальше рисовать от него: база на экране стоит на месте, а её медленный уход виден как сдвиг. Повторное нажатие - снова мерить базу по каждому кадру.' "
"data-en-title='Remember the current baseline level and draw from it from now on: the baseline stays put on screen, and its slow drift shows as an offset. Press again to measure the baseline from every frame.' "
"data-en='fix baseline'>зафиксировать базу</button></span>"
"<span class='ml mut' id=g_leg style=font-size:11.5px data-en='ruler: a click on the plot places "
"<b style=color:#7ee081>A</b> start, <b style=color:#f0c45a>B</b> peak, <b style=color:#ff8a82>C</b> end; marks can be dragged'>"
"линейка: щелчок по графику ставит <b style=color:#7ee081>A</b> начало, "
"<b style=color:#f0c45a>B</b> вершину, <b style=color:#ff8a82>C</b> конец; метки можно тащить</span>"
"</div></section>"
"<section class='panel pad'><canvas id=cv height=320></canvas>"
"<div id=g_rul class=grp style='margin-top:8px'>"
"<span id=rult class=mono style=font-size:12px></span><span class=ml></span>"
"<button id=rset class='btn sm' onclick=rulSet() title='Записать в прибор: до вершины = B−A, после вершины = C−B, отсчёты. "
"Действуют при способе «интегрирование».' "
"data-en-title='Write to the device: before peak = B−A, after peak = C−B, samples. "
"Used with the integration method.' data-en='set before / after peak'>"
"задать до / после вершины</button>"
"<button class='btn sm' onclick=rulClr() data-en='clear'>сбросить</button></div></section>"
"<section class='panel pad'><div id=sc class=mono style=min-height:110px></div></section>"
/* КАЛИБРОВКА ПО ЭНЕРГИИ - только на вкладке «Спектр»: линии изотопа ищет
   и подгоняет страница (cal*), прибор хранит результат (/calib) */
"<section class='panel pad' id=g_cal>"
/* снаружи - только кнопка и сводка в строку; всё остальное - в calbody */
"<div class=grp><button class='btn sm' id=cbt onclick=calTab() title='Калибровка по энергии, линии, мощность дозы, блок детектирования, проба KCl' "
"data-en-title='Energy calibration, lines, dose rate, detector block, KCl sample' data-en='Calibration'>Калибровка</button>"
"<span id=csum class=mut style=font-size:12px></span></div>"
"<div id=calbody hidden>"
"<div class=grp style=margin-top:10px>"
"<b style=color:#e8efe2 data-en='Energy calibration'>Калибровка по энергии</b>"
"<span class=lbl data-en='isotope'>изотоп</span>"
"<select id=cis><option value=ra226>Ra-226 (352, 609, 1120, 1764)</option>"
"<option value=cs137>Cs-137 (32, 662)</option><option value=th232>Th-232 (239, 583, 911, 2615)</option>"
"<option value=cs137k40>Cs-137 + K-40 (662, 1461)</option><option value=co60>Co-60 (1173, 1332)</option></select>"
"<button class='btn sm' onclick=calAuto() title='Найти линии изотопа в спектре, подогнать пики и построить шкалу E(канал). В прибор она попадает кнопкой «Сохранить в настройки»' "
"data-en-title='Find the isotope lines in the spectrum, fit the peaks and build the E(channel) scale. It goes to the device with the “Save to settings” button' data-en='Calibrate'>Откалибровать</button>"
"<button class='btn sm' id=cbl onclick=calLines() title='Отметить на графике линии изотопа: использованные для калибровки - ярко, остальные - бледно' "
"data-en-title='Mark the isotope lines on the plot: the ones used for the calibration - bright, the others - faint' data-en='Show calibration lines'>Показать линии калибровки</button>"
"<button class='btn sm green' id=cbs onclick=calSaveBtn() title='Сохранить калибровку в приборе: она переживёт перезагрузку и пойдёт в выгружаемые файлы' "
"data-en-title='Save the calibration in the device: it survives a reboot and goes into the exported files' data-en='Save to settings'>Сохранить в настройки</button>"
"</div>"
"<div id=cst style=font-size:12.5px></div>"
"<div id=cmsg style=font-size:12.5px></div>"
"<div id=cout class=mono style='font-size:12px;overflow-x:auto'></div>"
"<div id=cman></div>"
/* мощность дозы по спектру (mca_dose.h): средняя с «Сброса» и поправка k */
"<div class=grp style=margin-top:14px><b style=color:#e8efe2 data-en='Dose rate'>Мощность дозы</b>"
"<span id=dsv style=font-size:12.5px></span></div>"
"<div class=grp style=margin-top:4px><span class=lbl title='Под этот кристалл подогнана функция G(E): вес события в дозе' "
"data-en-title='The G(E) function, the weight of an event in the dose, is fitted to this crystal' data-en='detector block'>блок детектирования</span>"
"<select id=ddet onchange=doseDet()><option value=0>NaI &Oslash;25&times;25</option><option value=1>NaI &Oslash;30&times;40</option><option value=2>NaI &Oslash;30&times;70</option><option value=3>NaI &Oslash;40&times;40</option><option value=4>NaI &Oslash;63&times;63</option><option value=5>NaI &Oslash;80&times;80</option></select></div>"
"<div class=grp style=margin-top:4px><span class=lbl title='Положите дозиметр рядом с детектором, нажмите «Сброс», подождите несколько минут и впишите его показание' "
"data-en-title='Put a dosimeter next to the detector, press “Reset”, wait a few minutes and enter its reading' data-en='dosimeter reading, µSv/h'>показание дозиметра, мкЗв/ч</span>"
"<input id=ddm type=text inputmode=decimal style=width:96px>"
"<button class='btn sm' onclick=doseFit() data-en='Fit k'>Подогнать k</button>"
"<button class='btn sm' onclick=doseK(1) data-en='k = 1'>k = 1</button>"
"<span id=dmsg style=font-size:12.5px></span></div>"
/* проба KCl: фон пустого домика и проба - два набора одинаковой длины;
   пик 1461 кэВ против модели кристалла (kcl* в скрипте) -> поправка k */
"<div class=grp style=margin-top:14px><b style=color:#e8efe2 data-en='KCl sample'>Проба KCl</b>"
"<span class=mut style=font-size:12px data-en='pure dry KCl in a cylindrical jar, the detector end face on the lid centre'>"
"чистый сухой KCl в цилиндрической банке, детектор торцом на крышке по центру</span></div>"
"<div class=grp style=margin-top:4px>"
"<span class=lbl data-en='KCl, g'>KCl, г</span><input id=kms type=text inputmode=decimal value=400 style=width:64px>"
"<span class=lbl data-en='jar Ø, mm'>банка Ø, мм</span><input id=kdi type=text inputmode=decimal value=114 style=width:56px>"
"<span class=lbl data-en='layer, mm'>слой, мм</span><input id=kht type=text inputmode=decimal value=45 style=width:56px>"
"<span class=lbl title='Стенка банки плюс расстояние от торца блока до кристалла (обычно 2-6 мм)' "
"data-en-title='The jar wall plus the distance from the block end to the crystal (usually 2-6 mm)' data-en='salt to crystal, mm'>от соли до кристалла, мм</span>"
"<input id=kgp type=text inputmode=decimal value=5 style=width:48px></div>"
"<div class=grp style=margin-top:4px>"
"<button class='btn sm' onclick=kclBg() title='Пустой домик: «Сброс», набор, затем эта кнопка - спектр фона запомнит браузер' "
"data-en-title='Empty castle: “Reset”, acquire, then this button - the browser stores the background spectrum' data-en='Store background'>Запомнить фон</button>"
"<button class='btn sm' onclick=kclRun() title='Проба в домике: «Сброс», набор столько же, затем эта кнопка' "
"data-en-title='Sample in the castle: “Reset”, acquire as long, then this button' data-en='Compute from sample'>Посчитать по пробе</button>"
"<button class='btn sm green' id=kbk disabled onclick=kclSave() data-en='Save k'>Записать k</button>"
"<span id=kbg class=mut style=font-size:12px></span></div>"
"<div id=kmsg style=font-size:12.5px;margin-top:4px></div>"
"</div></section>"
/* МОНИТОРИНГ CPS - только на вкладке «Спектр» */
"<section class='panel pad' id=g_mon>"
"<div class=grp style=margin-bottom:10px>"
"<b style=color:#e8efe2 data-en='CPS monitor'>Мониторинг CPS</b>"
"<span class=mut id=mtime style=font-size:11.5px></span><span class=ml></span>"
"<span class=lbl title='По скольку секунд складывать отсчёты в одну точку графика. Шаг меньше секунды - из быстрой истории прибора: по 0.1 с за последние 10 минут' "
"data-en-title='How many seconds of samples go into one point of the plot. A step below a second comes from the fast history of the device: 0.1 s samples over the last 10 minutes' data-en='averaging, s'>усреднение, с</span>"
"<select id=mint onchange=mstp()><option value=0.1>0.1</option><option value=0.25>0.25</option><option value=0.5>0.5</option>"
"<option value=1 selected>1</option><option value=2>2</option><option value=5>5</option><option value=10>10</option>"
"<option value=30>30</option><option value=60>60</option><option value=300>300</option><option value=600>600</option>"
"<option value=3600>3600</option></select>"
"<span class=lbl title='Сколько времени видно на графике. Колесо мыши меняет масштаб, двойной щелчок возвращает к этому окну' "
"data-en-title='How much time the plot shows. The mouse wheel changes the zoom, double click returns to this window' data-en='window'>окно</span>"
"<select id=mwin onchange=mwsel()><option value=30000 data-en='30 s'>30 с</option><option value=120000 data-en='2 min'>2 мин</option>"
"<option value=600000 data-en='10 min'>10 мин</option><option value=1800000 data-en='30 min'>30 мин</option>"
"<option value=3600000 data-en='1 h'>1 ч</option><option value=21600000 data-en='6 h'>6 ч</option></select>"
"<span class=lbl title='Скользящее среднее (оранжевая линия): по скольким точкам' "
"data-en-title='Moving average (orange line): over how many points' data-en='SMA window'>окно SMA</span>"
"<input id=msma type=number min=2 max=200 value=10 onchange=mdraw()>"
"<button class='btn sm' onclick=mcsv() title='Точки графика файлом: время, длительность, импульсы, CPS, погрешность' "
"data-en-title='Plot points as a file: time, duration, counts, CPS, error'>CSV</button>"
"<button class='btn sm' onclick=mclr() data-en='clear history'>очистить историю</button></div>"
"<div class=mcards>"
"<div class=mcard><div class=lbl data-en='Current CPS'>Текущий CPS</div><div class='big ac' id=mc1>&mdash;</div>"
"<div class=mut id=mc1s></div></div>"
"<div class=mcard><div class=lbl data-en='Mean CPS (history)'>Средний CPS (история)</div><div class='big ac' id=mc2>&mdash;</div>"
"<div class=mut id=mc2s></div></div>"
"<div class=mcard><div class=lbl data-en='Last interval'>Последний интервал</div><div class='big ac' id=mc3>&mdash;</div>"
"<div class=mut id=mc3s></div></div></div>"
"<canvas id=mcv height=260 style=margin-top:10px></canvas>"
"<div class=mut style=font-size:11px;margin-top:4px>"
"<span style=color:#34d3c0>&#9644;</span> <span data-en='intervals'>интервалы</span> &nbsp;"
"<span style=color:#ff8c1a>&#9644;</span> SMA &nbsp; "
"<span data-en='wheel - zoom, drag - move into the past, double click - back to the window and now'>"
"колесо - масштаб, перетаскивание - сдвиг в прошлое, двойной щелчок - к окну и текущему моменту</span></div>"
"<div class=mtab><table><thead><tr><th data-en='Time'>Время</th><th>&Delta;t, <span data-en='s'>с</span></th>"
"<th data-en='Counts'>Импульсы</th><th>CPS</th><th>&delta;, %</th></tr></thead><tbody id=mtb></tbody></table></div>"
"</section>"
"</div>"
"<script>i18n();"
/* Настройки прибора, которые нужны осциллографу: окна L/G (трапеция и
   запас слева), порог, полярность. Таблица - на странице «Настройки»;
   здесь значения перечитываются раз в 5 с. */
"var PC={trap_L:20,trap_G:44,threshold:20,polarity:0};"
"function ldpc(){fetch('/cfg').then(function(r){return r.json()}).then(function(p){PC=p}).catch(function(){})}"
"ldpc();setInterval(ldpc,5000);"
"function cmd(c){fetch('/cmd?do='+c)}"
/* Старт/Стоп относятся к открытой вкладке: на «Спектре» - набор
   спектра, на «Осциллографе» - только осциллограф. На паузе осциллограф
   не опрашивается и держит последний кадр (линейка работает). */
/* RLOCK - как MDLOCK для режима: ответ /stat, ушедший с прибора ДО
   команды, иначе вернул бы прежнее состояние, и опрос осциллографа и
   подпись в шапке на секунду дёрнулись бы обратно */
/* Режим прибора общий: осциллограф и набор спектра одновременно не
   идут. «Старт» на «Осциллографе» при идущем наборе - только после
   предупреждения: набор останавливается (накопленный спектр остаётся),
   прибор переходит в осциллограф. «Старт» на «Спектре» возвращает
   прибор в набор. Раньше режим менялся уже при открытии вкладки, и
   осциллограф запускался сам, а набор молча вставал. */
"function DSC(){return window.DM==1||window.DM==3}"
"function devmode(v){window.DM=v;window.MDLOCK=Date.now()+1500;return fetch('/cfg?mode='+v)}"
"function run(on){window.RLOCK=Date.now()+1500;"
"if(isScope()){if(on){"
"if(!DSC()&&window.SPRUN&&!confirm(L('Набор спектра остановится: накопленный спектр сохранится, продолжить набор можно кнопкой «Старт» на вкладке «Спектр». Запустить осциллограф?',"
"'Spectrum acquisition will stop: the accumulated spectrum is kept, acquisition can be resumed with “Start” on the “Spectrum” tab. Start the oscilloscope?')))return;"
"window.SRUN=1;window.SPRUN=0;window.LASTJ=null;"
"fetch('/cmd?do=stop').then(function(){return devmode(SCM)}).then(function(){cmd('scope_start')})}"
"else{window.SRUN=0;cmd('scope_stop')}runChip(on,1)}"
"else{if(on&&DSC())devmode(0).then(function(){cmd('start')});else cmd(on?'start':'stop');"
"window.SPRUN=on?1:0;runChip(on,0)}}"
/* подпись в шапке: состояние той работы, что на открытой вкладке */
"function runChip(on,sc){"
"document.getElementById('rundot').className=on?'dot':'dot off';"
"document.getElementById('runtxt').textContent=sc?(on?L('осциллограф идёт','scope running'):L('осциллограф на паузе','scope paused')):"
"(on?L('идёт набор','acquiring'):L('остановлен','stopped'));"
"document.getElementById('runchip').className=on?'chip':'chip warn'}"
/* Выгрузка спектра файлом. Часов у прибора нет - время для файла
   берётся отсюда: Unix-время и смещение пояса в минутах. Ответ идёт
   как вложение, поэтому страница никуда не уходит. */
"function expo(f){var d=new Date();"
"location.href='/export.'+f+'?t='+Math.floor(d/1000)+'&tz='+(-d.getTimezoneOffset())+"
/* калибровка - в файл (XML, N42, SPE, CSV), только если она от этих настроек */
"(calOk()?CAL.c.map(function(v,i){return '&c'+i+'='+v}).join(''):'')}"
/* MDLOCK - окно, в течение которого ответы /stat не трогают DM: ответ,
   ушедший с прибора ДО новой команды, вернул бы прежний режим. */
"var cv=document.getElementById('cv'),cx=cv.getContext('2d');"
/* Поле графика: сверху TOPM, снизу BOTM под шкалу каналов.
   Обе функции обязаны считать высоту одинаково, иначе кривая
   разъедется с сеткой. */
"var TOPM=10,BOTM=22;"
"function plotH(H){return H-TOPM-BOTM}"
/* Шаг подписей: округляем до 1/2/5 x 10^n, чтобы числа были
   круглыми при любом количестве каналов. */
"function nicestep(span,want){var raw=span/want;"
"var p=Math.pow(10,Math.floor(Math.log(raw)/Math.LN10)),n=raw/p;"
"return (n<=1?1:n<=2?2:n<=5?5:10)*p}"
/* Спектр: лёгкая заливка под кривой и чёткая линия
   сверху. Сплошные столбцы давали тяжёлую «плиту» и топили форму. */
"function line(a,col,mn,mx,W,H,lw){if(!a||!a.length)return;"
"var sp=(mx-mn)||1,Hp=plotH(H),pts=[];"
"for(var i=0;i<a.length;i++){var v=(a[i]-mn)/sp;pts.push([i*W/a.length,H-BOTM-v*Hp])}"
"cx.beginPath();cx.moveTo(pts[0][0],H-BOTM);"
"for(var i=0;i<pts.length;i++)cx.lineTo(pts[i][0],pts[i][1]);"
"cx.lineTo(pts[pts.length-1][0],H-BOTM);cx.closePath();cx.fillStyle=col+'1e';cx.fill();"
"cx.beginPath();for(var i=0;i<pts.length;i++)i?cx.lineTo(pts[i][0],pts[i][1]):cx.moveTo(pts[i][0],pts[i][1]);"
"cx.strokeStyle=col;cx.lineWidth=lw||1;cx.stroke()}"
"function grid(W,H,mn,mx,lbl,lg,nch){cx.lineWidth=1;"
"cx.font='11px ui-monospace,monospace';var Hp=plotH(H);"
/* горизонтальные линии с подписями счёта */
"for(var k=0;k<=4;k++){var y=TOPM+k*Hp/4;"
"cx.strokeStyle='rgba(255,255,255,.05)';cx.beginPath();"
"cx.moveTo(0,y);cx.lineTo(W,y);cx.stroke();"
"var v=mx-(mx-mn)*k/4;"
"if(lg)v=Math.round(Math.exp(v)-1);else v=Math.round(v);"
"cx.fillStyle='#6b756a';cx.fillText(v,3,y-3)}"
/* ШКАЛА КАНАЛОВ СНИЗУ */
/* ось в кэВ: засечки на круглых энергиях, место - по обратной калибровке */
"if(nch>0&&window.CALAX&&calOk()){var e0=Math.max(0,calE(0)),e1=calE(nch),es=nicestep(e1-e0,8);cx.textAlign='center';"
"for(var e=Math.ceil(e0/es)*es;e<=e1;e+=es){var x=calCh(e)*W/nch;if(!(x>=0&&x<=W))continue;"
"cx.strokeStyle='rgba(255,255,255,.035)';cx.beginPath();cx.moveTo(x,TOPM);cx.lineTo(x,H-BOTM);cx.stroke();"
"cx.strokeStyle='#283024';cx.beginPath();cx.moveTo(x,H-BOTM);cx.lineTo(x,H-BOTM+4);cx.stroke();"
"cx.fillStyle='#6b756a';cx.fillText(Math.round(e),Math.min(Math.max(x,14),W-14),H-8)}"
"cx.textAlign='left';cx.fillStyle='#34d3c0';cx.fillText(L('кэВ','keV'),W-28,H-BOTM-4)}else "
"if(nch>0){var st=nicestep(nch,8);cx.textAlign='center';"
"for(var c=0;c<=nch;c+=st){var x=c*W/nch;"
"cx.strokeStyle='rgba(255,255,255,.035)';cx.beginPath();"
"cx.moveTo(x,TOPM);cx.lineTo(x,H-BOTM);cx.stroke();"
"cx.strokeStyle='#283024';cx.beginPath();"
"cx.moveTo(x,H-BOTM);cx.lineTo(x,H-BOTM+4);cx.stroke();"
"cx.fillStyle='#6b756a';"
"cx.fillText(c,Math.min(Math.max(x,14),W-14),H-8)}"
/* Последняя засечка - полное число каналов. Рисуем, только если
   до неё осталось место: иначе она налезает на предыдущую подпись
   (при 1024 каналах шаг 200 давал "1000" и "1024" друг на друге). */
"if(nch%st && (nch%st)*W/nch>34){cx.strokeStyle='#283024';cx.beginPath();"
"cx.moveTo(W-1,H-BOTM);cx.lineTo(W-1,H-BOTM+4);cx.stroke();"
"cx.fillStyle='#6b756a';cx.fillText(nch,W-14,H-8)}"
"cx.textAlign='left'}"
"cx.strokeStyle='#283024';cx.lineWidth=1;cx.strokeRect(0.5,TOPM+0.5,W-1,plotH(H));"
"if(lbl){cx.fillStyle='#6b756a';cx.fillText(lbl,W-150,TOPM+11)}}"
"function gridS(W,H,ymn,ymx,ns,lbl,us,x0,nd){nd=nd||5;"
"cx.font='11px ui-monospace,monospace';"
/* горизонтальные линии с подписями амплитуды; nd - число клеток */
"for(var k=0;k<=nd;k++){var y=10+k*(H-30)/nd;"
"cx.strokeStyle='rgba(255,255,255,.05)';cx.beginPath();cx.moveTo(28,y);cx.lineTo(W,y);"
"cx.stroke();cx.fillStyle='#6b756a';"
"cx.fillText(Math.round(ymx-(ymx-ymn)*k/nd),2,y+4)}"
/* вертикальные линии: по времени, если известен шаг отсчёта
   (us - микросекунд на отсчёт), иначе по номерам отсчётов. Номера
   считаются от x0 - индекса момента синхронизации (x0 < 0 - без
   синхронизации, тогда от левого края); шаг круглый, и 0 всегда
   попадает на линию сетки. */
"if(us){var span=ns*us,ts=nicestep(span,10);"
"for(var k=0;k*ts<=span*1.0001;k++){var t=k*ts,x=28+(t/us)*(W-30)/ns;"
"cx.strokeStyle='rgba(255,255,255,.035)';cx.beginPath();cx.moveTo(x,10);"
"cx.lineTo(x,H-20);cx.stroke();"
"cx.fillStyle='#6b756a';cx.fillText(+t.toFixed(2),x-6,H-6)}"
"cx.fillStyle='#6b756a';cx.fillText(L('мкс · ','µs · ')+(+ts.toFixed(3))+L(' мкс/дел',' µs/div'),W-170,22)}"
"else{var o0=x0>0?x0:0,stp=Math.max(1,Math.round(nicestep(ns,10)));"
"for(var k=Math.ceil(-o0/stp);o0+k*stp<=ns;k++){var x=28+(o0+k*stp)*(W-30)/ns;"
"cx.strokeStyle='rgba(255,255,255,.035)';cx.beginPath();cx.moveTo(x,10);"
"cx.lineTo(x,H-20);cx.stroke();"
"cx.fillStyle='#6b756a';cx.fillText(k*stp,x-6,H-6)}"
"cx.fillStyle='#6b756a';cx.fillText(L('отсч · ','smp · ')+stp+L(' отсч/дел',' smp/div')+(x0>=0?L(' · 0 = синхр.',' · 0 = trigger'):''),W-230,22)}"
"if(lbl){cx.fillStyle='#6b756a';cx.fillText(lbl,W-170,H-6)}}"
"function lineS(a,col,ymn,ymx,W,H,lw){if(!a||!a.length)return;"
"cx.strokeStyle=col;cx.lineWidth=lw||1.4;cx.beginPath();"
"var sp=(ymx-ymn)||1,ns=a.length-1;"
"for(var i=0;i<a.length;i++){var x=28+i*(W-30)/ns,"
"y=(H-20)-((a[i]-ymn)/sp)*(H-30);i?cx.lineTo(x,y):cx.moveTo(x,y)}"
"cx.stroke()}"
"function hline(val,mn,mx,W,H,col,txt){var sp=(mx-mn)||1;"
"var y=H-((val-mn)/sp)*(H-20)-10;if(y<0||y>H)return;"
"cx.strokeStyle=col;cx.lineWidth=1;cx.setLineDash([5,4]);cx.beginPath();"
"cx.moveTo(0,y);cx.lineTo(W,y);cx.stroke();cx.setLineDash([]);"
"cx.fillStyle=col;cx.font='11px ui-monospace,monospace';"
"cx.fillText(txt,W-90,y-4)}"
"function draw(a,log){var W=cv.width=cv.clientWidth,H=cv.height;"
"cx.clearRect(0,0,W,H);if(!a||!a.length)return;"
"var m=1;for(var i=0;i<a.length;i++)if(a[i]>m)m=a[i];"
"if(log){var b=[],lm=Math.log(1+m);"
"for(var i=0;i<a.length;i++)b[i]=Math.log(1+a[i]);"
"grid(W,H,0,lm,L('лог. шкала','log scale'),1,a.length);"
"line(b,'#34d3c0',0,lm,W,H)}else{"
"grid(W,H,0,m,L('линейная шкала','linear scale'),0,a.length);"
"line(a,'#34d3c0',0,m,W,H)}}"
"function draw2(a,fx,o){o=o||{};var W=cv.width=cv.clientWidth,H=cv.height;"
"cx.clearRect(0,0,W,H);if(!a||!a.length)return;"
/* база: если передали - по участку перед фронтом; иначе среднее по окну */
"var bl=o.bl;if(bl===undefined){bl=0;for(var i=0;i<a.length;i++)bl+=a[i];bl/=a.length}"
"var sig=[],smx=0,smn=0;"
"for(var i=0;i<a.length;i++){sig[i]=a[i]-bl;"
"if(sig[i]>smx)smx=sig[i];if(sig[i]<smn)smn=sig[i]}"
/* «вся шкала» - настоящий диапазон АЦП 0..4095 в выбранном отсчёте;
   иначе - по данным. При отсчёте не от базы ноль оси далеко от сигнала,
   и тянуть к нему масштаб нельзя. */
/* «кодов/клетку»: 8 клеток, база (o.b0 - где она в координатах графика)
   на первой линии снизу, при импульсах вниз - сверху. Масштаб от кадра к
   кадру не меняется, поэтому база стоит на месте; что не влезло в
   экран, обрезается по краю, и рядом пишется, насколько вышло. */
"var cd=+document.getElementById('ydiv').value||0,ym,ymn,nd=5;"
"if(fx){ym=4095-bl;ymn=-bl}"
"else if(cd>0){var b0=o.b0||0;nd=8;if(NEG()){ym=b0+cd;ymn=ym-8*cd}else{ymn=b0-cd;ym=ymn+8*cd}}"
"else if(o.abs){var sp=Math.max(8,smx-smn);ym=Math.ceil(smx+sp*0.15);"
"ymn=Math.floor(smn-sp*0.15)}"
"else{ym=Math.max(8,Math.ceil(smx*1.2/5)*5);ymn=Math.min(-4,Math.round(smn*1.2))}"
"gridS(W,H,ymn,ym,a.length-1,o.us?'':L('отсчёты от базовой линии','samples from baseline'),o.xs?0:o.us,o.tg,nd);"
"var cs=sig.map(function(v){return v>ym?ym:(v<ymn?ymn:v)});"
"lineS(cs,'#34d3c0',ymn,ym,W,H,1.2);"
"cx.fillStyle='#f0c45a';"
"if(smx>ym)cx.fillText('▲ '+L('выше экрана: до ','above the screen: up to ')+Math.round(smx),34,22);"
"if(smn<ymn)cx.fillText('▼ '+L('ниже экрана: до ','below the screen: down to ')+Math.round(smn),34,H-26);"
/* метка момента синхронизации */
"if(o.tg>=0){var xt=28+o.tg*(W-30)/Math.max(1,a.length-1);"
"cx.strokeStyle='#6ea8ff';cx.setLineDash([3,3]);cx.beginPath();"
"cx.moveTo(xt,10);cx.lineTo(xt,H-20);cx.stroke();cx.setLineDash([]);"
"cx.fillStyle='#6ea8ff';cx.fillText('▼',xt-4,10)}"
/* Трапеция на экран не выводится: она считается в приборе, а на
   картинке мало что добавляла. Масштаб отдаём линейке. */
"return {ymn:ymn,ym:ym}}"
/* Нормируем на длину окна, как это делает прошивка: иначе картинка
   и числа в браузере не совпадали бы с каналами спектра. */
/* Окно и зазор здесь - wL/wG, а не L/G: имя L занято функцией перевода
   L() (см. LJS), локальное L перекрыло бы её (проверяет check_i18n.py). */
"function trapOf(a,wL,wG){var t=[],acc=0;"
"for(var n=0;n<a.length;n++){var v=a[n],"
"vL=(n-wL>=0)?a[n-wL]:a[0],vG=(n-wG>=0)?a[n-wG]:a[0],"
"vGL=(n-wG-wL>=0)?a[n-wG-wL]:a[0];var d=v-vL-vG+vGL;"
"acc+=d;t[n]=acc/wL}return t}"
/* ИЗМЕРИТЕЛЬ ПИКА (поля "пик от"/"пик до").
   Считает центр тяжести, ширину на полувысоте и разрешение в
   процентах - это объективная замена разглядыванию формы импульса.
   Полувысота отсчитывается от ЛИНИИ ФОНА, проведённой по краям
   выделенного участка: без вычитания подложки комптоновский
   пьедестал завышал бы FWHM. */
"function peakInfo(a){window.PKMARK=null;window.PKINFO=null;"
"var A=+document.getElementById('pka').value|0,"
"B=+document.getElementById('pkb').value|0;"
"if(A<0)A=0;if(B>a.length-1)B=a.length-1;"
"if(!(B>A+4))return '';"
/* фон - прямая по краям участка, вычитаем её из всех каналов */
"var bg0=a[A],bg1=a[B],v=[];"
"for(var i=A;i<=B;i++)v[i-A]=a[i]-(bg0+(bg1-bg0)*(i-A)/(B-A));"
/* Сглаживание по трём каналам ТОЛЬКО для поиска высоты и полувысоты:
   одиночный шумовой выброс завышал максимум, и полувысота уезжала
   вверх - FWHM выходила заниженной (на модели 57 вместо 61). */
"var s=[];for(var i=0;i<v.length;i++){"
"var l=i>0?v[i-1]:v[i],r=i<v.length-1?v[i+1]:v[i];s[i]=(l+v[i]+r)/3}"
"var mx=-1,pk=0;for(var i=0;i<s.length;i++)if(s[i]>mx){mx=s[i];pk=i}"
"if(mx<=0)return ' &nbsp; <span style=color:#ff8a82>'+L('в этом диапазоне пика нет','no peak in this range')+'</span>';"
/* центр тяжести и площадь считаем по НЕсглаженным данным */
"var sw=0,swi=0;"
"for(var i=0;i<v.length;i++)if(v[i]>0){sw+=v[i];swi+=v[i]*(A+i)}"
"var cen=sw>0?swi/sw:A+pk;"
"var half=mx/2;"
"function cross(from,dir){var pv=s[from];"
"for(var i=from+dir;i>=0&&i<s.length;i+=dir){"
"if(s[i]<half)return A+i-dir*(half-s[i])/((pv-s[i])||1);"
"pv=s[i]}return A+(dir<0?0:s.length-1)}"
"var xl=cross(pk,-1),xr=cross(pk,1);"
"var fw=xr-xl;"
"if(!(fw>0))return ' &nbsp; <span style=color:#ff8a82>'+L('полувысота не найдена - расширьте диапазон','half maximum not found - widen the range')+'</span>';"
"var res=cen>0?100*fw/cen:0;"
"window.PKMARK=[A,B,xl,xr];var fk=calOk()?calE(xr)-calE(xl):0;"
"window.PKINFO={cen:cen,fw:fw,res:res,fk:fk,rk:calOk()?100*fk/calE(cen):0};"
"return '<br><b>'+L('пик:</b> центр ','peak:</b> centroid ')+cen.toFixed(1)+L(' кан',' ch')+' &nbsp; '+"
"'FWHM '+fw.toFixed(1)+L(' кан',' ch')+' &nbsp; <b>'+(calOk()?L('по каналам ','by channels '):L('разрешение ','resolution '))+res.toFixed(2)+' %</b>'+"
/* С калибровкой: энергия, FWHM в кэВ и разрешение от ЭНЕРГИИ линии.
   Процент по каналам зависит от нуля шкалы (у разных способов от +30 до
   -24 кэВ, замер 2026-11-06) - сравнивать по нему способы нечестно. */
"(calOk()?' &nbsp; <b style=color:#34d3c0>'+calE(cen).toFixed(1)+L(' кэВ, FWHM ',' keV, FWHM ')+(calE(xr)-calE(xl)).toFixed(1)+"
"L(' кэВ = ',' keV = ')+(100*(calE(xr)-calE(xl))/calE(cen)).toFixed(2)+L(' % по энергии',' % by energy')+'</b>':'')+"
"' <span style=opacity:.6>('+L('площадь за вычетом фона ','net area ')+Math.round(sw)+')</span>'}"
/* Границы участка (оранжевым) и точки полувысоты (зелёным). */
"function drawPk(nch,W,H){var m=window.PKMARK;if(!m)return;"
"cx.save();cx.setLineDash([4,3]);cx.lineWidth=1;"
"cx.strokeStyle='#f0c45a';"
"[m[0],m[1]].forEach(function(c){var x=c*W/nch;"
"cx.beginPath();cx.moveTo(x,TOPM);cx.lineTo(x,H-BOTM);cx.stroke()});"
"cx.strokeStyle='#34d3c0';"
"[m[2],m[3]].forEach(function(c){var x=c*W/nch;"
"cx.beginPath();cx.moveTo(x,TOPM);cx.lineTo(x,H-BOTM);cx.stroke()});"
"cx.restore()}"
/* ОСЦИЛЛОГРАФ. Прибор присылает окно с моментом синхронизации tg
   (-1 = свободный пуск). На экран идёт кусок длиной «развёртка»,
   момент синхронизации - на пятой части ширины, как у осциллографа. */
/* диапазон синхронизации по амплитуде строкой, '' - фильтр выключен */
"function AF(){var lo=+document.getElementById('amin').value||0,hi=+document.getElementById('amax').value||0;"
"return hi>0?Math.min(lo,hi)+'&ndash;'+Math.max(lo,hi):''}"
"function drawScope(j,wait){var a=j.d,sc=document.getElementById('sc');"
"if(!a||!a.length){cv.width=cv.clientWidth;cx.clearRect(0,0,cv.width,cv.height);"
"sc.innerHTML=wait?(AF()?L('ждущий режим: импульсов с амплитудой ','normal mode: no pulses with amplitude ')+AF()+"
"L(' кодов ещё не было',' codes yet')+(j.rej>0?L(' (отброшено ',' (rejected ')+j.rej+')':''):"
"L('ждущий режим: фронта &ge; ','normal mode: no edge &ge; ')+j.lvl+"
"L(' кодов ещё не было &mdash; снизьте «синхр. по фронту»',' codes yet &mdash; lower “trigger on edge”')):"
"L('ждём данные...','waiting for data...');return}"
"var fh=window.REALHZ||8e6,tg=j.tg;window.LASTJ=j;window.LASTW=wait;"
/* wL/wG, а не L/G: локальное L перекрыло бы функцию перевода L(), и весь
   текст под графиком пропадал (исключение глотал опрос прибора) */
"var wL=PC.trap_L,wG=PC.trap_G;"
"var Wn=Math.min(+document.getElementById('zm').value||512,a.length);"
/* без синхронизации пропускаем запас слева: там трапеция ещё не встала */
"var st=tg>=0?tg-Math.round(Wn*0.2):(wL|0)+(wG|0)+16;"
"if(st<0)st=0;if(st+Wn>a.length)st=a.length-Wn;"
"var tr=trapOf(a,wL,wG);"
/* база и шум - по участку ДО фронта: в окне с импульсом среднее завышено */
"var e=tg>0?Math.max(1,tg-8):a.length,bl=0,nv=0;"
"for(var i=0;i<e;i++)bl+=a[i];bl/=e;"
"for(var i=0;i<e;i++)nv+=(a[i]-bl)*(a[i]-bl);var sd=Math.sqrt(nv/e);"
/* отсчёт оси: от базы, от середины шкалы или от нуля кодов */
/* B - уровень базы, от которого рисуем: зафиксированный кнопкой или
   измеренный по этому кадру; b0 - где он на оси Y при выбранном отсчёте */
"var R=window.YREF|0,B=window.BFIX||bl,ref=R==0?B:(R==1?2048:0),da=a.slice(st,st+Wn);"
"var rr=draw2(da,document.getElementById('fx').checked,{bl:ref,b0:B-ref,abs:R!=0,"
"us:1e6/fh,xs:window.XAX==1,tg:tg>=0?tg-st:-1});"
/* кадр для линейки: z - индекс момента синхронизации на экране */
"window.SCV={n:Wn,z:tg>=0?tg-st:0,W:cv.width,a:da,sg:NEG()?-1:1,us:1e6/fh,bl:bl};"
"if(rr)rulDraw(rr,ref);rulText();"
"var mn=a[0],mx=a[0];for(var i=0;i<a.length;i++){if(a[i]<mn)mn=a[i];if(a[i]>mx)mx=a[i]}"
"var sg=NEG()?-1:1,nmx=-1e9;for(var i=wL+wG;i<e;i++)if(sg*tr[i]>nmx)nmx=sg*tr[i];"
"var thr=PC.threshold;"
"var sy=tg>=0?L('<b style=color:#34d3c0>синхронизирован</b> по фронту ','<b style=color:#34d3c0>triggered</b> on an edge of ')+j.rise+"
"L(' кодов (уровень ',' codes (level ')+j.lvl+')':"
"'<span style=color:#f0c45a>'+(AF()?L('импульсов с амплитудой ','no pulses with amplitude ')+AF()+L(' кодов',' codes'):"
"L('фронта &ge; ','no edge &ge; ')+j.lvl+L(' кодов',' codes'))+"
"L(' нет &mdash; свободный пуск',' &mdash; free run')+'</span>';"
/* возраст снимка - с поправкой на время с его прихода (по WebSocket
   старый кадр не повторяется, страница перерисовывает его сама) */
"var ag=j.age<0?-1:j.age+(j.rcv?Date.now()-j.rcv:0);"
"if(wait&&ag>1500)sy+=' &nbsp;<span style=opacity:.7>'+L('снимок ','captured ')+(ag/1000).toFixed(0)+L(' с назад',' s ago')+'</span>';"
/* высота показанного импульса и работа фильтра амплитуды */
"if(j.amp>=0)sy+=' &nbsp;'+L('высота импульса','pulse height')+' <b>'+j.amp+'</b> '+L('кодов','codes');"
"if(AF())sy+=' &nbsp;<span class=mut>'+L('фильтр ','filter ')+AF()+L(', отброшено с прошлого кадра ',', rejected since last frame ')+(j.rej||0)+'</span>';"
"sc.innerHTML=sy+"
"L('<br>на экране ','<br>on screen ')+Wn+L(' отсч = ',' smp = ')+(Wn/fh*1e6).toFixed(1)+L(' мкс из ',' µs of ')+(j.len||a.length)+L(' отсч = ',' smp = ')+"
"((j.len||a.length)/fh*1e6).toFixed(0)+L(' мкс записи',' µs recorded')+"
"(window.SFPS?L(' &nbsp; обновление ',' &nbsp; refresh ')+window.SFPS+L(' раз/с','/s'):'')+"
/* поток осциллографа и чем он идёт */
"(window.SKBPS?' &nbsp; '+L('поток ','stream ')+window.SKBPS+L(' кбит/с',' kbit/s')+' ('+(WSOK?'WebSocket':'HTTP')+')':'')+"
"L('<br>сигнал: мин ','<br>signal: min ')+mn+L('  макс ','  max ')+mx+L('  база ','  baseline ')+bl.toFixed(0)+(window.BFIX?L(' (зафиксирована ',' (fixed at ')+window.BFIX+L(', сдвиг ',', drift ')+((bl-window.BFIX)>=0?'+':'')+(bl-window.BFIX).toFixed(1)+')':'')+L('  шум(СКО) ','  noise(RMS) ')+sd.toFixed(1)+"
"((mx>=4095||mn<=0)?'  <b style=color:#ff8a82>'+L('упор в шкалу АЦП','ADC scale clipped')+'</b>':'')+"
/* сколько места осталось от базы до потолка и пола шкалы */
"L('<br>запас по шкале от базы: вверх <b>','<br>headroom from baseline: up <b>')+(4095-Math.round(bl))+L('</b>, вниз <b>','</b>, down <b>')+"
"Math.round(bl)+L('</b> кодов <span class=mut>(импульсы ','</b> codes <span class=mut>(pulses ')+(NEG()?L('вниз','down'):L('вверх','up'))+')</span>'+"
"(nmx>-1e9?L('<br>шум фильтра до импульса (трапеция считается в фоне): макс ','<br>filter noise before the pulse (trapezoid computed in the background): max ')+"
"nmx.toFixed(1)+L('  &nbsp; порог ','  &nbsp; threshold ')+thr+"
"(nmx>thr?'  <b style=color:#ff8a82>'+L('шум перебивает порог','noise exceeds threshold')+'</b>':"
"'  <b style=color:#34d3c0>'+L('порог выше шума','threshold above noise')+'</b>'):'')}"
/* ЛИНЕЙКА. Метки A - начало, B - вершина, C - конец импульса. Хранятся
   в отсчётах от момента синхронизации, поэтому стоят на месте от кадра
   к кадру (без синхронизации - от левого края экрана). Щелчок ставит
   следующую метку, после третьей - начинает заново; метку можно тащить.
   B при установке прилипает к вершине рядом со щелчком: на глаз её
   легко поставить на отсчёт мимо. */
"var RUL=[null,null,null],RCOL=['#7ee081','#f0c45a','#ff8a82'],HOV=null,DRAG=-1,RAF=0;"
/* Холст общий со спектром: осциллограф рисуем, только если он открыт -
   и при вызове, и в момент кадра (вкладку могли сменить за это время).
   Без этого уход курсора с холста на «Спектре» на мгновение рисовал
   поверх спектра последний кадр осциллографа. */
"function scRedraw(){if(RAF||!window.LASTJ||!isScope())return;"
"RAF=requestAnimationFrame(function(){RAF=0;if(isScope())drawScope(window.LASTJ,window.LASTW)})}"
"function rPx(i){var g=window.SCV;return 28+i*(g.W-30)/Math.max(1,g.n-1)}"
"function rIdx(x){var g=window.SCV;"
"return Math.max(0,Math.min(g.n-1,Math.round((x-28)*(g.n-1)/(g.W-30))))}"
"function snapPk(i){var g=window.SCV,w=Math.max(3,Math.round(g.n*0.015)),b=i;"
"for(var k=Math.max(0,i-w);k<=Math.min(g.n-1,i+w);k++)if(g.sg*g.a[k]>g.sg*g.a[b])b=k;return b}"
"function isScope(){var m=document.getElementById('md').value;return m=='1'||m=='3'}"
"function rulDraw(rr,ref){var g=window.SCV,H=cv.height,sp=(rr.ym-rr.ymn)||1;"
"function Y(v){return (H-20)-((v-ref-rr.ymn)/sp)*(H-30)}"
"cx.save();cx.font='11px ui-monospace,monospace';cx.lineWidth=1;"
"for(var k=0;k<3;k++){if(RUL[k]==null)continue;var i=RUL[k]+g.z;"
"if(i<0||i>=g.n)continue;var x=rPx(i);"
"cx.strokeStyle=RCOL[k];cx.setLineDash([4,3]);cx.beginPath();cx.moveTo(x,10);"
"cx.lineTo(x,H-20);cx.stroke();cx.setLineDash([]);cx.fillStyle=RCOL[k];"
"cx.fillText('ABC'[k],x+3,22);cx.beginPath();cx.arc(x,Y(g.a[i]),3.5,0,7);cx.fill()}"
/* перекрестие под курсором с подписью */
"if(HOV!=null&&HOV<g.n){var x=rPx(HOV),r=HOV-g.z,d=Math.round(g.a[HOV]-g.bl);"
"cx.strokeStyle='rgba(230,236,226,.35)';cx.beginPath();cx.moveTo(x,10);cx.lineTo(x,H-20);cx.stroke();"
"var t=(r>0?'+':'')+r+L(' отсч · ',' smp · ')+(r*g.us).toFixed(2)+L(' мкс · ',' µs · ')+g.a[HOV]+L(' кодов (',' codes (')+"
"(d>=0?'+':'')+d+L(' от базы)',' from baseline)');"
"var tw=cx.measureText(t).width+8,tx=Math.min(x+8,g.W-tw-2);"
"cx.fillStyle='rgba(12,16,12,.88)';cx.fillRect(tx,30,tw,17);"
"cx.fillStyle='#e6ece2';cx.fillText(t,tx+4,42)}"
"cx.restore()}"
"function rulText(){var g=window.SCV,t='';"
"function ds(a,b){return (b-a)+L(' отсч (',' smp (')+((b-a)*g.us).toFixed(2)+L(' мкс)',' µs)')}"
"for(var k=0;k<3;k++)if(RUL[k]!=null)t+='ABC'[k]+' '+(RUL[k]>0?'+':'')+RUL[k]+'   ';"
"if(RUL[0]!=null&&RUL[1]!=null)t+=L('| фронт A→B ','| rise A→B ')+ds(RUL[0],RUL[1])+'   ';"
"if(RUL[1]!=null&&RUL[2]!=null)t+=L('| спад B→C ','| fall B→C ')+ds(RUL[1],RUL[2])+'   ';"
"if(RUL[0]!=null&&RUL[2]!=null)t+=L('| всего A→C ','| total A→C ')+ds(RUL[0],RUL[2])+'   ';"
"var ib=RUL[1]!=null?RUL[1]+g.z:-1;"
"if(ib>=0&&ib<g.n)t+=L('| высота B ','| height B ')+Math.round(g.sg*(g.a[ib]-g.bl))+L(' кодов от базы',' codes from baseline');"
"document.getElementById('rult').textContent=t||L('меток нет: щёлкните по графику','no marks: click on the plot');"
"document.getElementById('rset').disabled=!(RUL[0]!=null&&RUL[1]!=null&&RUL[2]!=null)}"
"function rulClr(){RUL=[null,null,null];scRedraw()}"
/* измеренное линейкой - сразу в прибор: до вершины = B-A, после = C-B
   (таблица настроек - на отдельной странице, полей здесь нет) */
"function rulSet(){if(RUL[0]==null||RUL[1]==null||RUL[2]==null)return;"
"var a=Math.max(0,RUL[1]-RUL[0]),b=Math.max(1,RUL[2]-RUL[1]),e=document.getElementById('rset');"
"fetch('/cfg?int_rise='+a+'&int_fall='+b).then(function(r){return r.json()}).then(function(p){PC=p;"
"e.textContent=L('задано: до ','set: before ')+p.int_rise+L(', после ',', after ')+p.int_fall;"
"setTimeout(function(){e.textContent=L('задать до / после вершины','set before / after peak')},3000)})"
".catch(function(){e.textContent=L('нет связи','no connection')})}"
"cv.addEventListener('pointerdown',function(e){if(!isScope()||!window.SCV)return;"
"var g=window.SCV,x=e.offsetX,i=rIdx(x),best=-1,bd=8;"
"for(var k=0;k<3;k++)if(RUL[k]!=null){var dd=Math.abs(rPx(RUL[k]+g.z)-x);if(dd<bd){bd=dd;best=k}}"
"if(best>=0){DRAG=best;cv.setPointerCapture(e.pointerId);return}"
"var k=RUL[0]==null?0:RUL[1]==null?1:RUL[2]==null?2:0;"
"if(k==0)RUL=[null,null,null];"
"RUL[k]=(k==1?snapPk(i):i)-g.z;scRedraw()});"
"cv.addEventListener('pointermove',function(e){if(!isScope()||!window.SCV)return;"
"var i=rIdx(e.offsetX);HOV=i;if(DRAG>=0)RUL[DRAG]=i-window.SCV.z;scRedraw()});"
"cv.addEventListener('pointerup',function(){DRAG=-1});"
"cv.addEventListener('pointerleave',function(){if(DRAG<0){HOV=null;if(isScope())scRedraw()}});"
/* Вкладки режимов и видимость управления: каждому режиму - свои поля. */
/* Авто и ждущий - один осциллограф: вкладка одна, режим развёртки
   переключается сегментом. SCM помнит последний выбранный. Режим 2
   (прежние «Импульсы») для прошивки тот же набор спектра - показываем
   его как вкладку «Спектр». */
/* Галочка «вся шкала»: выбор помнится в браузере. */
"function vis(){try{localStorage.setItem('mca_fx',document.getElementById('fx').checked?1:0)}catch(e){}"
"window.LASTSIG='';showCtl()}"
/* ось Y осциллографа: 0 - от базы, 1 - от середины шкалы, 2 - коды АЦП */
"function setRef(v){window.YREF=v;"
"for(var i=0;i<3;i++)document.getElementById('rf'+i).className=i==v?'on':'';"
"try{localStorage.setItem('mca_ref',v)}catch(e){}poll()}"
/* ось X осциллографа: 0 - микросекунды, 1 - отсчёты от синхронизации.
   Перерисует следующий кадр осциллографа - он приходит каждые 100 мс. */
"function setX(v){window.XAX=v;"
"for(var i=0;i<2;i++)document.getElementById('bx'+i).className=i==v?'on':'';"
"try{localStorage.setItem('mca_xax',v)}catch(e){}}"
"function NEG(){return PC.polarity==1}"
/* кодов на клетку - запоминает браузер; база фиксируется до повторного нажатия */
"function ydivSet(){try{localStorage.setItem('mca_ydiv',document.getElementById('ydiv').value)}catch(e){}scRedraw()}"
"function bfix(){var g=window.SCV;window.BFIX=window.BFIX?0:(g?Math.round(g.bl):0);bfixLbl();scRedraw()}"
"function bfixLbl(){var b=document.getElementById('bfix');"
"b.textContent=window.BFIX?L('база ','baseline ')+window.BFIX+L(': отпустить',': release'):L('зафиксировать базу','fix baseline');"
"b.className=window.BFIX?'btn sm amber':'btn sm'}"
"try{document.getElementById('fx').checked=localStorage.getItem('mca_fx')==='1';"
"window.YREF=+(localStorage.getItem('mca_ref')||0);"
"window.XAX=+(localStorage.getItem('mca_xax')||0);"
"document.getElementById('ydiv').value=localStorage.getItem('mca_ydiv')||'0'}catch(e){window.YREF=0;window.XAX=0}"
"for(var i=0;i<3;i++)document.getElementById('rf'+i).className=i==window.YREF?'on':'';"
"for(var i=0;i<2;i++)document.getElementById('bx'+i).className=i==window.XAX?'on':'';"
"var SCM=1;"
"function showCtl(){var m=document.getElementById('md').value,sp=(m=='0'||m=='2'),sc=!sp;"
"cv.style.cursor='crosshair';cv.style.touchAction=sc?'none':'pan-y';"
"var v={g_sp:sp,g_sc:sc,g_zm:sc,g_leg:sc,g_rul:sc,g_big:sp,bclr:sp,st:sp,g_mon:sp,g_cal:sp};"
"for(var k in v){var e=document.getElementById(k);if(e)e.style.display=v[k]?'':'none'}"
"if(sp)setTimeout(mdraw,0)}"
"function hl(){var m=document.getElementById('md').value,g=(m=='3')?'1':(m=='2'?'0':m);"
"if(m=='1'||m=='3')SCM=+m;"
"var t=document.querySelectorAll('#tabs [data-md]');"
"for(var i=0;i<t.length;i++)t[i].className=t[i].getAttribute('data-md')==g?'on':'';"
"document.getElementById('bauto').className=m=='1'?'on':'';"
"document.getElementById('bwait').className=m=='3'?'on':'';"
"showCtl()}"
"function tab(v){document.getElementById('md').value=v;hl();window.LASTSIG='';poll()}"
/* авто / ждущий: если осциллограф уже идёт - сразу на приборе */
"function scm(v){SCM=v;if(DSC())devmode(v);tab(v)}"
"function setLog(v){document.getElementById('lg').checked=!!v;"
"document.getElementById('blin').className=v?'':'on';"
"document.getElementById('blog').className=v?'on':'';poll()}"
"function hms(s){s=Math.floor(s);var h=Math.floor(s/3600),m=Math.floor(s%3600/60),c=s%60;"
"return (h<10?'0':'')+h+':'+(m<10?'0':'')+m+':'+(c<10?'0':'')+c}"
"hl();"
"function poll(){"
"var md=document.getElementById('md').value;"
/* вкладка осциллографа, а прибор не в нём: холст пуст, под ним - что нажать */
"if(isScope()&&!DSC()&&window.DM!==undefined){cv.width=cv.clientWidth;cx.clearRect(0,0,cv.width,cv.height);window.LASTJ=null;"
"document.getElementById('sc').innerHTML=L('Осциллограф остановлен. «Старт» запустит его','The oscilloscope is stopped. “Start” launches it')+"
"(window.SPRUN?L(' &mdash; набор спектра при этом остановится.',' &mdash; spectrum acquisition will stop then.'):'.')}"
"if(md=='0'||md=='2'){mload();fetch('/spectrum').then(r=>r.json()).then(function(j){"
"window.LASTSPEC=j.d;specRender();calShow()})}"
"fetch('/stat').then(r=>r.json()).then(function(j){window.FHZ=j.freq;window.RUNMS=j.ms;"
/* Строка статуса: значения через разделители,
   подписи приглушены. Время, CPS и события - ещё и крупно в панели,
   состояние набора и частота - в шапке. */
"var dt=j.samp>0?100*j.dead/j.samp:0,cp=window.CPSFT>Date.now()-600?window.CPSF:j.cps;"
"document.getElementById('st').innerHTML="
"'<span class=ac>CPS <span id=stc>'+cp+'</span></span><span class=sep>|</span>'+"
"'<span id=std class=ac'+(window.DOSEF?'':' hidden')+' title=\"'+(window.DOSET||'')+'\">'+(window.DOSEF||'')+'</span><span id=stds class=sep'+(window.DOSEF?'':' hidden')+'>|</span>'+"
"'<span class=w>'+j.ev+' <span class=mut>'+L('событий','events')+'</span></span><span class=sep>|</span>'+"
"'<span title=\"'+L('зависит от Перезапуск и Поиск пика','depends on Re-arm and Peak search')+'\"><span class=mut>'+"
"L('мёртвое','dead')+'</span> '+dt.toFixed(1)+'%</span>'+"
/* загрузка обработки - та же, что в «Диагностике»: около 100% - не
   успевает, пойдут потерянные чанки */
"'<span class=sep>|</span><span title=\"'+L('Доля времени, когда обработка потока занята чанками (как в «Диагностике»). '+"
"'Около 100% - не успевает, пойдут потерянные чанки: снизьте частоту.','Share of time stream processing is busy with chunks (as in “Diagnostics”). '+"
"'Near 100% - it cannot keep up, chunks will be lost: lower the rate.')+'\"><span class=mut>'+L('загрузка','load')+'</span> '+"
"((j.load||0)/10).toFixed(0)+'%</span>'+"
/* векторная обработка не прошла самопроверку при старте - предупредить */
"(j.vec===0?'<span class=sep>|</span><span style=color:#f0c45a title=\"'+L('Векторные команды не прошли '+"
"'самопроверку при старте, обработка идёт обычным кодом - медленнее.','Vector instructions failed the self-test at start, '+"
"'processing runs plain code - slower.')+'\">'+L('векторы выкл.','vectors off')+'</span>':'');"
"document.getElementById('bt').textContent=hms(j.ms/1000);"
/* часы прибора - в заголовке мониторинга */
"var mt=document.getElementById('mtime');if(mt&&j.tsrc!==undefined){var dd=new Date(j.tnow);"
"mt.textContent=L('время прибора ','device time ')+(j.tnow?p2(dd.getHours())+':'+p2(dd.getMinutes())+':'+"
"p2(dd.getSeconds()):L('не задано','not set'))+(j.tsrc==1?' (SNTP)':j.tsrc==2?L(' (от браузера)',' (from browser)'):'')}"
"if(j.mode!==undefined&&!(window.MDLOCK>Date.now()))window.DM=j.mode;"
"if(!(window.RLOCK>Date.now())){if(j.srun!==undefined)window.SRUN=j.srun;window.SPRUN=j.run;"
"if(isScope())runChip(DSC()&&j.srun,1);else runChip(!DSC()&&j.run,0)}"
"document.getElementById('fchip').textContent=(j.freq/1e6).toFixed(2)+L(' МГц',' MHz');"
/* при открытии страницы - вкладка того, что прибор делает сейчас */
"if(!window.VINIT&&j.mode!==undefined){window.VINIT=1;if(j.mode==1||j.mode==3){SCM=+j.mode;tab(SCM)}}"
"window.REALHZ=j.freq})}"
/* ОСЦИЛЛОГРАФ опрашивается отдельно от спектра и статуса: следующий
   запрос уходит, как только отрисован предыдущий, но не чаще раза в
   SPER мс. Частота сама подстраивается под длину развёртки и скорость
   сети, и запросы не копятся в очередь. Просим только кусок под
   развёртку плюс запас L+G+16 слева, чтобы трапеция у края экрана
   успела установиться. Ответ двоичный: семь int32 и отсчёты по 2 байта. */
/* 50 мс - до 20 кадров в секунду: окна коротких развёрток прибор держит
   во внутренней памяти и отдаёт без копии, ответ занимает единицы мс */
"var SPER=50;"
"function sparse(b){var v=new DataView(b),n=(b.byteLength-28)>>1,d=new Array(n);"
"for(var i=0;i<n;i++)d[i]=v.getUint16(28+2*i,true);"
"return {tg:v.getInt32(0,true),rise:v.getInt32(4,true),age:v.getInt32(8,true),"
"lvl:v.getInt32(12,true),len:v.getInt32(16,true),amp:v.getInt32(20,true),"
"rej:v.getInt32(24,true),d:d}}"
/* настройки осциллографа строкой - одни и те же для /scope и WebSocket */
"function scq(){var Wn=+document.getElementById('zm').value||512,"
"M=(PC.trap_L|0)+(PC.trap_G|0)+16;"
"return 'lvl='+(+document.getElementById('tl').value||30)+'&n='+(Wn+M)+'&pre='+(Math.round(Wn*0.2)+M)+"
"'&amin='+(+document.getElementById('amin').value||0)+'&amax='+(+document.getElementById('amax').value||0)}"
/* Кадр WebSocket: 10 x int32 (как у /scope, плюс номер снимка, число
   отсчётов и способ записи: 0 - uint16, 1 - сжатие), затем отсчёты.
   Сжатие (scope_codec.c в прошивке): полубайты, старший первым; первый
   отсчёт - 3 полубайта целиком, дальше разность -7..7 одним полубайтом,
   полубайт 8 - следом отсчёт целиком (3 полубайта). */
"function wdec(u,n){var d=new Array(n),p=0,m=u.length*2,i=0,x=0;"
"function nb(){var b=u[p>>1],c=(p&1)?b&15:b>>4;p++;return c}"
"if(n>0&&m>=3){x=(nb()<<8)|(nb()<<4)|nb();d[i++]=x;"
"while(i<n&&p<m){var c=nb();if(c==8){if(p+3>m)break;x=(nb()<<8)|(nb()<<4)|nb()}else x+=c>7?c-16:c;d[i++]=x}}"
"d.length=i;return d}"
"function wparse(b){if(b.byteLength<40)return null;"
"var v=new DataView(b),n=v.getInt32(32,true),e=v.getInt32(36,true),d;"
"if(e==1)d=wdec(new Uint8Array(b,40),n);"
"else{n=Math.min(n,(b.byteLength-40)>>1);d=new Array(n);for(var i=0;i<n;i++)d[i]=v.getUint16(40+2*i,true)}"
"return {tg:v.getInt32(0,true),rise:v.getInt32(4,true),age:v.getInt32(8,true),lvl:v.getInt32(12,true),"
"len:v.getInt32(16,true),amp:v.getInt32(20,true),rej:v.getInt32(24,true),seq:v.getInt32(28,true),d:d}}"
/* Пришёл кадр (любым путём). Рисуем НЕ ЧАЩЕ РАЗА В SPER мс, на любой
   развёртке: большие кадры приходят по TCP пачками, и если рисовать каждый
   сразу, браузер не успевал (32768 точек на кадр), кадры копились, и
   обновление то разгонялось, то падало. Пришедший раньше срока кадр ждёт,
   а следующий его заменяет - рисуется всегда самый свежий. Ответ мог прийти
   уже на «Спектре» - тогда не рисуем. */
"var SBYTES=0,SFR=0,SPEND=null,SLASTD=0,STMR=0;"
"function sframe(j,md,nb){SBYTES+=nb;if(!isScope())return;j.rcv=Date.now();SPEND=[j,md];"
"var w=SLASTD+SPER-Date.now();if(w<=0)sdraw();else if(!STMR)STMR=setTimeout(sdraw,w)}"
"function sdraw(){STMR=0;var p=SPEND;SPEND=null;if(!p||!isScope())return;"
"SLASTD=Date.now();SFR++;drawScope(p[0],p[1]=='3')}"
/* Раз в секунду: частота обновления - сколько кадров НАРИСОВАНО за секунду
   (прежнее сглаживание мгновенных скоростей в пачках завышало цифру), и
   поток в кбит/с. В ждущем режиме по WebSocket старые кадры не
   повторяются, поэтому возраст снимка обновляем перерисовкой. */
"setInterval(function(){window.SFPS=SFR;SFR=0;window.SKBPS=Math.round(SBYTES*8/1000);SBYTES=0;"
"if(WSOK&&isScope()&&window.LASTJ&&window.LASTW)scRedraw()},1000);"
/* WEBSOCKET: прибор сам шлёт новые кадры. Настройки уходят при изменении
   и раз в 2 с (чтобы сервер не счёл соединение простаивающим). Не
   открылся или оборвался - опрос /scope, новая попытка через 3 с. */
"var WS=null,WSOK=0,WSQ='',WST=0;"
"function wsOpen(){if(!window.WebSocket)return;"
"try{WS=new WebSocket((location.protocol=='https:'?'wss://':'ws://')+location.host+'/ws')}catch(e){WS=null;return}"
"WS.binaryType='arraybuffer';"
"WS.onopen=function(){WSOK=1;WSQ=''};"
"WS.onclose=function(){WSOK=0;WS=null;setTimeout(wsOpen,3000)};"
"WS.onmessage=function(ev){if(typeof ev.data=='string')return;"
"if(!DSC()||(window.SRUN===0&&window.LASTJ))return;"
"var j=wparse(ev.data);if(j)sframe(j,document.getElementById('md').value,ev.data.byteLength)}}"
"function sloop(){var md=document.getElementById('md').value,sc=(md=='1'||md=='3'),"
/* пауза: кадры не берём, на экране последний */
"on=sc&&DSC()&&!(window.SRUN===0&&window.LASTJ);"
"if(!on)SPEND=null;"
"if(WSOK){var q=scq()+'&run='+(on?1:0),t=Date.now();"
"if(q!=WSQ||t-WST>2000){try{WS.send(q);WSQ=q;WST=t}catch(e){}}"
"setTimeout(sloop,100);return}"
"if(!on){setTimeout(sloop,250);return}"
/* без WebSocket - опрос: следующий запрос, как только отрисован
   предыдущий, но не чаще раза в SPER мс */
"var t0=Date.now();"
"fetch('/scope?'+scq()).then(function(r){return r.arrayBuffer()}).then(function(b){"
"if(b.byteLength>=28)sframe(sparse(b),md,b.byteLength)})"
".catch(function(){}).then(function(){setTimeout(sloop,Math.max(10,SPER-(Date.now()-t0)))})}"
/* МОНИТОРИНГ CPS. Прибор копит односекундные отсчёты набора (кольцо 6 ч
   в PSRAM, mca_hist.c), страница раз в секунду дочитывает новые
   (/hist?from=номер) и сама складывает их в интервалы. Время отсчёта -
   по часам браузера: сейчас минус его возраст по часам прибора, так что
   для графика часы прибора не обязательны. CPS интервала - импульсы,
   делённые на время, пока реально шёл набор. */
"function p2(x){return (x<10?'0':'')+x}"
/* Две истории прибора: MH - по 1 с (6 ч), MF - по 0.1 с (10 мин, /hist?f=1)
   для шага графика меньше секунды. Быструю страница дочитывает 5 раз в
   секунду (из опроса CPS), пока выбран такой шаг. */
"var MH={ep:-1,nx:0,t:[],c:[],d:[],busy:0,bn:0,B:null,f:0,sp:1000},MF={ep:-1,nx:0,t:[],c:[],d:[],busy:0,f:1,sp:100,tk:0},"
"MV={span:0,end:0,live:0,tA:0,pw:1,x0:0,hx:null};"
"function mstep(){return Math.round((+document.getElementById('mint').value||1)*1000)}"
"function mfast(){return mstep()<1000}"
"function msrc(){return mfast()?MF:MH}"
"function mstp(){MV.end=0;if(mfast())mload1(MF,1);mdraw()}"
"function mload(){mload1(MH,0);if(mfast())mload1(MF,1)}"
"function mload1(H,f){if(H.busy)return;H.busy=1;"
"fetch('/hist?'+(f?'f=1&':'')+'from='+H.nx).then(function(r){return r.arrayBuffer()}).then(function(b){H.busy=0;"
"if(b.byteLength<28)return;var v=new DataView(b),ep=v.getUint32(0,true),fs=v.getUint32(4,true),"
"nx=v.getUint32(8,true),nds=v.getUint32(12,true),n=v.getUint32(16,true),len=v.getUint32(20,true)||21600,now=Date.now();"
"if(ep!==H.ep){H.ep=ep;H.t=[];H.c=[];H.d=[];if(H==msrc())MV.end=0;H.o=null}"
"var o0=mofs(H,now,nds);"
"for(var i=0;i<n&&38+10*i<=b.byteLength;i++){var o=28+10*i;"
"H.t.push(o0+v.getUint32(o,true)*100);H.c.push(v.getUint32(o+4,true));H.d.push(v.getUint16(o+8,true))}"
"var ex=H.t.length-len;if(ex>0){H.t.splice(0,ex);H.c.splice(0,ex);H.d.splice(0,ex)}"
"H.nx=fs+n;if(H.nx<nx)mload1(H,f);else if(H==msrc()||!f)mdraw()}).catch(function(){H.busy=0})}"
/* Сдвиг часов прибора относительно браузера - один на всю историю.
   Раньше время отсчёта считалось от момента своего запроса («сейчас минус
   возраст»), и у каждой порции был свой сдвиг: задержка сети и округление
   до 0.1 с. Отсчёт у границы секунды перескакивал в соседнюю - в одной
   секунде два отсчёта, следующая пустая, и график рвался (у прибора шаг
   отсчётов ровно 1.0 с: 280 из 284). Пересчёт - только при расхождении
   больше 2 с (перевели часы компьютера). */
"function mofs(H,now,nds){var o=now-nds*100;if(H.o==null||Math.abs(o-H.o)>2000)H.o=o;return H.o}"
/* Интервалы по step мс, выровненные по часам; отдаются только законченные
   (конец не позже последнего отсчёта). T - время конца отсчёта. */
"function mbuck(T,C,D,step,sp){var o={t:[],c:[],d:[]},k=-1,cur=NaN,n=T.length;if(!n)return o;"
"var last=T[n-1];for(var i=0;i<n;i++){var b=Math.floor(T[i]/step)*step;if(b+step>last+(sp||1000)/2)break;"
"if(b!==cur){cur=b;o.t.push(b);o.c.push(0);o.d.push(0);k++}o.c[k]+=C[i];o.d[k]+=D[i]}return o}"
"function mrel(c){return c>0?(100/Math.sqrt(c)).toFixed(2):'—'}"
"function mcard(id,v,s){document.getElementById(id).textContent=v==null?'—':v.toFixed(1);"
"document.getElementById(id+'s').textContent=s}"
"function mdraw(){var gm=document.getElementById('g_mon');if(!gm||gm.style.display=='none')return;"
"var st=mstep(),HS=msrc(),B=mbuck(HS.t,HS.c,HS.d,st,HS.sp),"
"n=MH.t.length,N=B.t.length;MH.bn=N;MH.B=B;"
"var ct=0,dt=0;for(var i=0;i<n;i++){ct+=MH.c[i];dt+=MH.d[i]}"
"mcard('mc1',n&&MH.d[n-1]?MH.c[n-1]*1000/MH.d[n-1]:null,L('за последнюю секунду набора','over the last second of acquisition'));"
"mcard('mc2',dt?ct*1000/dt:null,dt?'δ '+mrel(ct)+' % · '+ct+L(' имп. за ',' counts in ')+hms(dt/1000):"
"L('история копится, пока идёт набор спектра','history accumulates while the spectrum is acquired'));"
"var lb=N-1;mcard('mc3',N&&B.d[lb]?B.c[lb]*1000/B.d[lb]:null,"
"N?'δ '+mrel(B.c[lb])+' % · '+B.c[lb]+L(' имп. за ',' counts in ')+(B.d[lb]/1000).toFixed(st<1000?2:1)+L(' с',' s'):'');"
"mchart();"
"var h='';for(var i=N-1;i>=Math.max(0,N-100);i--){var d=new Date(B.t[i]);"
"h+='<tr><td>'+p2(d.getHours())+':'+p2(d.getMinutes())+':'+p2(d.getSeconds())+(st<1000?'.'+Math.floor(d.getMilliseconds()/100):'')+'</td><td>'+(B.d[i]/1000).toFixed(st<1000?2:1)+"
"'</td><td>'+B.c[i]+'</td><td>'+(B.d[i]?(B.c[i]*1000/B.d[i]).toFixed(1):'—')+'</td><td>'+mrel(B.c[i])+'</td></tr>'}"
"document.getElementById('mtb').innerHTML=h}"
/* График: интервалы (с разрывом, где набора не было) и скользящее
   среднее в окне по времени (см. ниже); ось X - настоящее время. */
"function mchart(){var B=MH.B,c2=document.getElementById('mcv');if(!B||!c2)return;"
"var g=c2.getContext('2d'),W=c2.width=c2.clientWidth,H=c2.height,N=B.t.length,x0=58,x1=W-10,y0=10,y1=H-22;"
"g.clearRect(0,0,W,H);g.strokeStyle='#283024';g.lineWidth=1;g.strokeRect(x0+.5,y0+.5,x1-x0,y1-y0);"
"g.font='11px ui-monospace,monospace';g.textBaseline='middle';MV.pw=x1-x0;MV.x0=x0;"
"if(N<1){g.fillStyle='#6b756a';g.fillText(L('нет данных: история копится, пока идёт набор спектра',"
"'no data: history accumulates while the spectrum is acquired'),x0+10,y0+24);return}"
"var stp=mstep(),"
"w=Math.min(200,Math.max(2,+document.getElementById('msma').value|0)),Y=[],S=[],a=0;"
"for(var i=0;i<N;i++){Y.push(B.d[i]>0?B.c[i]*1000/B.d[i]:0);a+=Y[i];if(i>=w)a-=Y[i-w];S.push(a/Math.min(i+1,w))}"
/* Окно ПО ВРЕМЕНИ: MV.span мс, кончая MV.end (0 - текущий момент, окно
   едет за новыми данными). Раньше по умолчанию показывалась вся история, и
   график сжимался по мере накопления. Масштаб оси X от числа точек не
   зависит, остановки набора видны пустыми местами. */
"if(!MV.span)MV.span=+document.getElementById('mwin').value||30000;"
"var live=B.t[N-1]+stp,tB=MV.end||live,tA=tB-MV.span;MV.live=live;MV.tA=tA;"
"var s=0;while(s<N&&B.t[s]<tA)s++;var e=s;while(e<N&&B.t[e]<tB)e++;"
"function X(t){return x0+(x1-x0)*(t-tA)/MV.span}"
"var mn=1e30,mx=-1e30;for(var i=s;i<e;i++){mn=Math.min(mn,Y[i],S[i]);mx=Math.max(mx,Y[i],S[i])}"
"if(e<=s){mn=0;mx=1}if(mx-mn<1e-9)mx=mn+1;var pd=(mx-mn)*0.08;mn-=pd;mx+=pd;"
"function Yp(v){return y1-(y1-y0)*(v-mn)/(mx-mn)}"
"g.textAlign='right';"
"for(var k=0;k<=4;k++){var vv=mn+(mx-mn)*k/4,yy=Yp(vv);g.strokeStyle='rgba(255,255,255,.05)';g.beginPath();"
"g.moveTo(x0,yy);g.lineTo(x1,yy);g.stroke();g.fillStyle='#6b756a';g.fillText(vv>=100?vv.toFixed(0):vv.toFixed(1),x0-6,yy)}"
/* подписи времени: круглый шаг, по местному времени браузера */
"var TS=[1,2,5,10,15,30,60,120,300,600,900,1800,3600,7200,10800,21600],ts=TS[TS.length-1]*1000;"
"for(var k=0;k<TS.length;k++)if(TS[k]*1000>=MV.span/6){ts=TS[k]*1000;break}"
"var tz=new Date().getTimezoneOffset()*60000;g.textAlign='center';g.textBaseline='top';"
"for(var t=Math.ceil((tA-tz)/ts)*ts+tz;t<=tB;t+=ts){var xx=X(t),dd=new Date(t);"
"g.strokeStyle='rgba(255,255,255,.04)';g.beginPath();g.moveTo(xx,y0);g.lineTo(xx,y1);g.stroke();g.fillStyle='#6b756a';"
"g.fillText(p2(dd.getHours())+':'+p2(dd.getMinutes())+(ts<60000?':'+p2(dd.getSeconds()):''),xx,y1+5)}"
"g.save();g.beginPath();g.rect(x0,y0,x1-x0,y1-y0);g.clip();"
"g.strokeStyle='#34d3c0';g.lineWidth=1.4;g.beginPath();"
"for(var i=s;i<e;i++){var xx=X(B.t[i]),yy=Yp(Y[i]);"
"if(i>s&&B.t[i]-B.t[i-1]<=1.5*stp)g.lineTo(xx,yy);else g.moveTo(xx,yy)}g.stroke();"
"if(e-s<=300){g.fillStyle='#34d3c0';for(var i=s;i<e;i++){g.beginPath();g.arc(X(B.t[i]),Yp(Y[i]),2,0,6.3);g.fill()}}"
"g.strokeStyle='#ff8c1a';g.lineWidth=2;g.beginPath();"
"for(var i=s;i<e;i++){var xx=X(B.t[i]),yy=Yp(S[i]);"
"if(i>s&&B.t[i]-B.t[i-1]<=1.5*stp)g.lineTo(xx,yy);else g.moveTo(xx,yy)}g.stroke();g.restore();"
/* какое окно видно и не в прошлом ли мы */
"var sw=MV.span/1000,wt=sw<120?sw.toFixed(0)+L(' с',' s'):sw<7200?(sw/60).toFixed(1)+L(' мин',' min'):(sw/3600).toFixed(1)+L(' ч',' h');"
"g.textAlign='left';g.textBaseline='middle';g.fillStyle='#6b756a';"
"g.fillText(L('окно ','window ')+wt+(MV.end?L(' · прошлое, двойной щелчок - к текущему моменту',"
"' · past, double click - back to now'):''),x0+6,y1-10);"
/* подсказка под курсором: ближайший видимый интервал */
"if(e>s&&MV.hx!=null&&MV.hx>=x0&&MV.hx<=x1){var tt=tA+(MV.hx-x0)/(x1-x0)*MV.span,bi=s;"
"for(var i=s;i<e;i++)if(Math.abs(B.t[i]-tt)<Math.abs(B.t[bi]-tt))bi=i;"
"var xx=X(B.t[bi]),dd=new Date(B.t[bi]);g.strokeStyle='rgba(230,236,226,.35)';g.lineWidth=1;g.beginPath();"
"g.moveTo(xx,y0);g.lineTo(xx,y1);g.stroke();"
"var tx=p2(dd.getHours())+':'+p2(dd.getMinutes())+':'+p2(dd.getSeconds())+'  '+Y[bi].toFixed(1)+' cps  SMA '+"
"S[bi].toFixed(1)+'  δ '+mrel(B.c[bi])+' %';"
"var tw=g.measureText(tx).width+10,bx=Math.min(xx+8,x1-tw);g.fillStyle='rgba(12,16,12,.9)';g.fillRect(bx,y0+4,tw,18);"
"g.fillStyle='#e6ece2';g.fillText(tx,bx+5,y0+13)}}"
"function mwsel(){MV.span=+document.getElementById('mwin').value||30000;MV.end=0;mchart()}"
/* колесо - масштаб по времени вокруг курсора, перетаскивание - сдвиг в
   прошлое и обратно, двойной щелчок - к окну из списка и текущему моменту */
"(function(){var c2=document.getElementById('mcv'),dr=0,dx=0,dB=0;"
"c2.addEventListener('wheel',function(ev){if(!MH.bn)return;ev.preventDefault();"
"var fr=Math.min(1,Math.max(0,(ev.offsetX-MV.x0)/MV.pw)),tc=MV.tA+fr*MV.span,"
"stp=mstep(),"
"ns=Math.min(21600000,Math.max(Math.max(mfast()?1000:5000,3*stp),MV.span*(ev.deltaY<0?0.8:1.25))),nB=tc+(1-fr)*ns;"
"MV.span=ns;MV.end=nB>=MV.live?0:nB;mchart()},{passive:false});"
"c2.addEventListener('mousedown',function(ev){if(!MH.bn)return;dr=1;dx=ev.clientX;dB=MV.end||MV.live;"
"c2.style.cursor='grabbing'});"
"window.addEventListener('mousemove',function(ev){if(!dr)return;var B=MH.B,"
"nB=dB-(ev.clientX-dx)/MV.pw*MV.span,lo=(B&&B.t.length?B.t[0]:0)+MV.span*0.2;if(nB<lo)nB=lo;"
"MV.end=nB>=MV.live?0:nB;mchart()});"
"window.addEventListener('mouseup',function(){if(dr){dr=0;c2.style.cursor='grab'}});"
"c2.addEventListener('mousemove',function(ev){MV.hx=ev.offsetX;if(!dr)mchart()});"
"c2.addEventListener('mouseleave',function(){MV.hx=null;mchart()});"
"c2.addEventListener('dblclick',function(){mwsel()});c2.style.cursor='grab'})();"
/* выгрузка точек графика файлом */
"function mcsv(){var st=mstep(),HS=msrc(),B=mbuck(HS.t,HS.c,HS.d,st,HS.sp);"
"if(!B.t.length){alert(L('Нет данных','No data'));return}"
"var s='time,dt_s,counts,cps,rel_err_pct\\r\\n';"
"for(var i=0;i<B.t.length;i++){var d=new Date(B.t[i]),z=-d.getTimezoneOffset();"
"s+=d.getFullYear()+'-'+p2(d.getMonth()+1)+'-'+p2(d.getDate())+'T'+p2(d.getHours())+':'+p2(d.getMinutes())+':'+"
"p2(d.getSeconds())+'.'+String(d.getMilliseconds()+1000).slice(1)+(z>=0?'+':'-')+p2(Math.floor(Math.abs(z)/60))+':'+p2(Math.abs(z)%60)+','+(B.d[i]/1000).toFixed(3)+','+"
"B.c[i]+','+(B.d[i]?(B.c[i]*1000/B.d[i]).toFixed(3):'')+','+(B.c[i]?(100/Math.sqrt(B.c[i])).toFixed(3):'')+'\\r\\n'}"
"var a=document.createElement('a'),n=new Date();a.href=URL.createObjectURL(new Blob([s],{type:'text/csv'}));"
"a.download='mca_cps_'+n.getFullYear()+p2(n.getMonth()+1)+p2(n.getDate())+'_'+p2(n.getHours())+p2(n.getMinutes())+"
"p2(n.getSeconds())+'.csv';document.body.appendChild(a);a.click();"
"setTimeout(function(){URL.revokeObjectURL(a.href);a.remove()},1000)}"
"function mclr(){if(!confirm(L('Очистить историю CPS на приборе?','Clear the CPS history on the device?')))return;"
"fetch('/hist?clear=1').then(function(){mload()})}"
/* вкладка из ссылки с других страниц: #scope или #spectrum - только вид */
"if(location.hash=='#scope'){window.VINIT=1;tab(SCM)}else if(location.hash=='#spectrum'){window.VINIT=1;tab(0)}"
"setInterval(poll,1000);poll();wsOpen();sloop();"
/* CPS 10 раз в секунду: /cps - быстрый CPS прибора (окно 100 мс ... 1 с,
   при скачке счёта - 100 мс). Только на спектре и при видимой вкладке,
   не больше одного запроса сразу (медленная связь не копит очередь). */
/* МОЩНОСТЬ ДОЗЫ. Быстрая - в строке статуса из /cps (то же окно, что у
   быстрого CPS); средняя с «Сброса» и поправка k - в блоке калибровки из
   /dose раз в секунду. */
"function d3(v){return v>=100?v.toFixed(0):v>=10?v.toFixed(1):v>=1?v.toFixed(2):v.toFixed(3)}"
/* в строке статуса - мкЗв/ч за скользящие 10 с набора, раз в секунду (из
   /dose): точность задаёт длина окна, а не частота показа - при ~30 имп/с
   окно 1 с дало бы ±18 %, 10 с - ±6 %. После «Сброса» окно копится. */
"var DRING=[];"
"function doseRing(j){if(!j.ok||j.hs==null){DRING=[];window.DOSEF='';doseStd();return}"
"var z=DRING.length?DRING[DRING.length-1]:null;if(z&&(j.s<z.s||j.n<z.n))DRING=[],z=null;"
"if(!z||j.s>z.s)DRING.push({s:j.s,h:j.hs,r:j.ks,n:j.n});"
"while(DRING.length>2&&DRING[DRING.length-1].s-DRING[1].s>=10)DRING.shift();"
"var a=DRING[0],b=DRING[DRING.length-1],dt=b.s-a.s,dn=b.n-a.n;"
"if(DRING.length<2||dt<0.5){if(!z)window.DOSEF='';doseStd();return}"
"window.DOSEF=d3((b.h-a.h)*3600e-6/dt*j.k)+L(' мкЗв/ч',' µSv/h');"
"window.DOSET=L('за последние ','over the last ')+dt.toFixed(0)+L(' с набора, δ ',' s of acquisition, δ ')+(dn>0?(100/Math.sqrt(dn)).toFixed(0)+' %':'-')+"
"' · '+d3((b.r-a.r)*3600e-6/dt/8.764e-3*j.k)+L(' мкР/ч',' µR/h');doseStd()}"
"function doseStd(){var e=document.getElementById('std'),w=document.getElementById('stds');"
"if(e){e.textContent=window.DOSEF||'';e.title=window.DOSET||'';e.hidden=!window.DOSEF}if(w)w.hidden=!window.DOSEF}"
"var DOSE=null;"
"function doseLoad(){fetch('/dose').then(function(r){return r.json()}).then(function(j){DOSE=j;doseRing(j);doseShow()}).catch(function(){})}"
"function doseShow(){var e=document.getElementById('dsv'),j=DOSE;if(!e||!j)return;"
"var sd=document.getElementById('ddet');if(sd&&document.activeElement!==sd&&j.det!=null)sd.value=j.det;if(window.calSum)calSum();"
"if(!j.ok){e.innerHTML='<span class=mut>'+(j.why==2?L('калибровка по энергии набрана при других настройках - доза не считается',"
"'the energy calibration was made with other settings - the dose is not computed'):j.why==1?L('нужна калибровка по энергии, сохранённая в приборе',"
"'needs an energy calibration saved in the device'):L('доза не считается','the dose is not computed'))+'</span>';return}"
"e.innerHTML=L('средняя с «Сброса» ','mean since “Reset” ')+'<b>'+d3(j.h)+L(' мкЗв/ч',' µSv/h')+'</b> · '+d3(j.r)+L(' мкР/ч',' µR/h')+"
"' <span class=mut>'+L('за ','over ')+hms(j.s)+(j.n>0?', δ '+(100/Math.sqrt(j.n)).toFixed(1)+' %':'')+"
"L('; поправка k = ','; correction k = ')+(+j.k).toFixed(3)+'</span>'}"
"function doseDet(){var v=document.getElementById('ddet').value;fetch('/dose?det='+v).then(function(r){return r.json()}).then(function(j){DOSE=j;doseShow();"
"doseMsg(j.set?L('блок выбран; доза с «Сброса» смешана со старым блоком - нажмите «Сброс»','the block is selected; the dose since “Reset” is mixed with the old block - press “Reset”'):"
"L('блок не выбран','the block was not selected'),!j.set)}).catch(function(){doseMsg(L('нет связи с прибором','no connection with the device'),1)})}"
"function doseMsg(t,bad){var e=document.getElementById('dmsg');if(e)e.innerHTML=t?'<span style=color:'+(bad?'#ff8a82':'#7ee081')+'>'+t+'</span>':''}"
"function doseK(k){fetch('/dose?k='+k).then(function(r){return r.json()}).then(function(j){DOSE=j;doseShow();"
"doseMsg(j.set?L('поправка k = ','correction k = ')+(+j.k).toFixed(3)+L(' записана в прибор',' saved in the device'):"
"L('поправка не записана','the correction was not saved'),!j.set)}).catch(function(){doseMsg(L('нет связи с прибором','no connection with the device'),1)})}"
"function doseFit(){var v=+document.getElementById('ddm').value.trim().replace(',','.'),j=DOSE;"
"if(!(v>0)){doseMsg(L('впишите показание дозиметра, мкЗв/ч','enter the dosimeter reading, µSv/h'),1);return}"
"if(!j||!j.ok||!(j.h0>0)){doseMsg(L('доза ещё не считается','the dose is not computed yet'),1);return}"
"if(j.s<60){doseMsg(L('наберите хотя бы минуту после «Сброса»','acquire at least a minute after “Reset”'),1);return}"
"var k=v/j.h0;if(k<0.05||k>20){doseMsg(L('поправка ','correction ')+k.toFixed(3)+L(' вне 0.05...20 - проверьте показание и калибровку',' is outside 0.05...20 - check the reading and the calibration'),1);return}"
"doseK(k.toFixed(4))}"
"setInterval(function(){if(!isScope()&&!document.hidden)doseLoad()},1000);doseLoad();"
"var CPSB=0;setInterval(function(){if(CPSB||document.hidden||isScope())return;CPSB=1;"
"fetch('/cps').then(function(r){return r.json()}).then(function(j){CPSB=0;window.CPSF=j.cpsf;window.CPSFT=Date.now();"
"var e=document.getElementById('stc');if(e){e.textContent=j.cpsf;e.title=L('окно ','window ')+j.cpsw+L(' мс',' ms')}"
"if(mfast()&&++MF.tk%2==0)mload1(MF,1)}"
").catch(function(){CPSB=0})},100);"
/* КАЛИБРОВКА ПО ЭНЕРГИИ. Ядро (calcore.js в разработке: поиск линий,
   подгонка пиков, полином) проверено node на 14 спектрах Ra-226 и 4 - Cs-137
   против независимой подгонки гаусс + фон, и на прореженных спектрах:
   от 100 тыс. событий линии находятся верно, меньше - поиск отказывается
   (2026-11-06); tools/check_cal.py. */
"var CALISO={ra226:['Ra-226',[351.93,609.31,1120.29,1764.49]],"
"cs137:['Cs-137',[32.19,661.66]],"
"cs137k40:['Cs-137 + K-40',[661.66,1460.82]],"
"th232:['Th-232',[238.63,583.19,911.20,2614.51]],"
"co60:['Co-60',[1173.23,1332.49]]};"
"function calEv(c,x){var e=0;for(var i=c.length-1;i>=0;i--)e=e*x+c[i];return e}"
"function calDv(c,x){var d=0;for(var i=c.length-1;i>=1;i--)d=d*x+i*c[i];return d}"
"function calChv(c,E){var x=(E-c[0])/c[1];"
"for(var i=0;i<40&&c.length>2;i++){var d=calDv(c,x);if(!d)break;var f=calEv(c,x)-E;x-=f/d;if(Math.abs(f)<1e-6)break}"
"return x}"
"function calFit1(a,A,B){A=Math.max(0,Math.round(A));B=Math.min(a.length-1,Math.round(B));"
"if(B-A<8)return null;"
"var b0=(a[A]+a[A+1]+a[A+2])/3,b1=(a[B]+a[B-1]+a[B-2])/3,xa=A+1,xb=B-1,v=[],mx=0,pk=-1;"
"for(var i=A;i<=B;i++)v[i-A]=a[i]-(b0+(b1-b0)*(i-xa)/(xb-xa));"
"for(var i=A+1;i<B;i++){var s=(v[i-A-1]+v[i-A]+v[i-A+1])/3;if(s>mx){mx=s;pk=i}}"
"if(pk<0||mx<=3)return null;"
"var l=pk,r=pk;while(l>A&&v[l-1-A]>0.45*mx)l--;while(r<B&&v[r+1-A]>0.45*mx)r++;"
"if(r-l<2){l=Math.max(A,pk-1);r=Math.min(B,pk+1)}"
"var S=[0,0,0,0,0],T=[0,0,0];"
"for(var i=l;i<=r;i++){var y=v[i-A];if(y<=0)continue;var x=i-pk,ly=Math.log(y);"
"S[0]+=y;S[1]+=y*x;S[2]+=y*x*x;S[3]+=y*x*x*x;S[4]+=y*x*x*x*x;T[0]+=y*ly;T[1]+=y*ly*x;T[2]+=y*ly*x*x}"
"var M=[[S[0],S[1],S[2],T[0]],[S[1],S[2],S[3],T[1]],[S[2],S[3],S[4],T[2]]];"
"for(var i=0;i<3;i++){var m=i;for(var j=i+1;j<3;j++)if(Math.abs(M[j][i])>Math.abs(M[m][i]))m=j;"
"var t=M[i];M[i]=M[m];M[m]=t;if(!M[i][i])return null;"
"for(var j=0;j<3;j++)if(j!=i){var f=M[j][i]/M[i][i];for(var q=i;q<4;q++)M[j][q]-=f*M[i][q]}}"
"var p0=M[0][3]/M[0][0],p1=M[1][3]/M[1][1],p2=M[2][3]/M[2][2];"
"if(!(p2<0))return null;"
"var sg=Math.sqrt(-1/(2*p2)),mu=pk-p1/(2*p2);"
"if(!(mu>A&&mu<B))return null;"
"return {mu:mu,fw:2.3548*sg,h:Math.exp(p0-p1*p1/(4*p2)),area:Math.exp(p0-p1*p1/(4*p2))*sg*2.5066}}"
"function calHM(a,mu,fw){var sg=fw/2.3548,A=Math.max(1,Math.round(mu-3*sg)),B=Math.min(a.length-2,Math.round(mu+3*sg));"
"if(B-A<6)return 0;"
"var b0=(a[A-1]+a[A]+a[A+1])/3,b1=(a[B-1]+a[B]+a[B+1])/3,s=[];"
"for(var i=A;i<=B;i++){var bg=b0+(b1-b0)*(i-A)/(B-A),t=0;for(var j=-1;j<=1;j++)t+=a[i+j];s[i-A]=t/3-bg}"
"var pk=0;for(var i=1;i<s.length;i++)if(s[i]>s[pk])pk=i;var hm=s[pk]/2;if(!(hm>0))return 0;"
"var i=pk;while(i>0&&s[i-1]>=hm)i--;if(i==0)return 0;var xl=A+i-(s[i]-hm)/(s[i]-s[i-1]);"
"i=pk;while(i<s.length-1&&s[i+1]>=hm)i++;if(i==s.length-1)return 0;var xr=A+i+(s[i]-hm)/(s[i]-s[i+1]);"
"return xr-xl}"
"function calFit(a,A,B){var f=calFit1(a,A,B);if(!f)return null;"
"var g=calFit1(a,f.mu-1.27*f.fw,f.mu+1.27*f.fw);if(!g||Math.abs(g.mu-f.mu)>=f.fw)g=f;"
"var w=calHM(a,g.mu,g.fw);if(w>0)g.fw=w;return g}"
"function calCands(a){var n=a.length,s=[];"
"for(var i=0;i<n;i++){var t=0,c=0;for(var j=-2;j<=2;j++)if(i+j>=0&&i+j<n){t+=a[i+j];c++}s[i]=t/c}"
"var C=[];"
"for(var i=3;i<n-3;i++){if(s[i]<5)continue;var w=Math.max(3,Math.round(0.02*i)),ok=1;"
"for(var j=Math.max(0,i-w);j<=Math.min(n-1,i+w);j++)if(s[j]>s[i]||(s[j]==s[i]&&j<i)){ok=0;break}if(!ok)continue;"
"var g=Math.max(12,Math.round(0.15*i)),lo=s[i];"
"for(var j=Math.max(0,i-g);j<=Math.min(n-1,i+g);j++)if(s[j]<lo)lo=s[j];"
"if(s[i]>1.15*lo)C.push([i,s[i]])}return C}"
"function calFind(a,E){var C=calCands(a),M=[];"
"for(var p=0;p<E.length;p++)for(var q=p+1;q<E.length;q++)"
"for(var i=0;i<C.length;i++)for(var j=i+1;j<C.length;j++){"
"var k1=(E[q]-E[p])/(C[j][0]-C[i][0]),k0=E[p]-k1*C[i][0];"
"if(!(k1>0)||Math.abs(k0)>80)continue;"
"if(k0+k1*(a.length-1)>20000)continue;"
"var sc=0,m=[];"
"for(var k=0;k<E.length;k++){var x=(E[k]-k0)/k1,bi=-1,bd=Math.max(3,0.03*x);"
"for(var u=0;u<C.length;u++){var d=Math.abs(C[u][0]-x);if(d<=bd){bd=d;bi=u}}"
"if(bi>=0){var r=(C[bi][0]-x)/Math.max(3,0.03*x);sc+=1000-300*r*r+Math.log(1+C[bi][1]);m[k]=C[bi][0]}}"
"M.push({sc:sc,m:m,key:m.join(',')})}"
"M.sort(function(x,y){return y.sc-x.sc});"
"var need=E.length>=4?3:2,best=null,seen={},tried=0;"
"for(var t=0;t<M.length&&tried<8;t++){if(seen[M[t].key])continue;seen[M[t].key]=1;tried++;"
"var R=[];"
"for(var k=0;k<E.length;k++){if(M[t].m[k]===undefined)continue;var x=M[t].m[k],h=Math.max(6,0.08*x);"
"var f=calFit(a,x-h,x+h);"
"if(f&&f.area>=50&&f.fw>0.005*f.mu&&f.fw<0.5*f.mu)R.push({E:E[k],ch:f.mu,fw:f.fw,area:f.area})}"
"if(R.length<need)continue;"
"var c=calPoly(R);if(!c)continue;"
"var ws=R.map(function(l){return (calEv(c,l.ch+l.fw/2)-calEv(c,l.ch-l.fw/2))/Math.sqrt(l.E)}),"
"sp=Math.max.apply(null,ws)/Math.min.apply(null,ws),rs=0;"
"R.forEach(function(l){var d=(l.E-calEv(c,l.ch))/l.E;rs+=d*d});rs=Math.sqrt(rs/R.length);"
"var chk=!R.some(function(l){return l.E<100});"
"if((chk&&!(sp<2.2))||calEv(c,a.length-1)>20000)continue;"
"var q2=1000*R.length-(chk?300*(sp-1):0)-3e4*rs+1e-3*M[t].sc;"
"if(!best||q2>best.q)best={q:q2,R:R}}"
"return best?best.R:[]}"
"function calPoly(P){var n=P.length;if(!n)return null;"
"if(n==1)return [0,P[0].E/P[0].ch];"
"var d=n>=4?3:2,A=[],y=[];"
"for(var i=0;i<n;i++){var r=[];for(var k=0;k<d;k++)r.push(Math.pow(P[i].ch,k));A.push(r);y.push(P[i].E)}"
"var M=[];for(var r=0;r<d;r++){M[r]=[];for(var c=0;c<d;c++){var s=0;for(var i=0;i<n;i++)s+=A[i][r]*A[i][c];M[r][c]=s}"
"var s=0;for(var i=0;i<n;i++)s+=A[i][r]*y[i];M[r][d]=s}"
"for(var i=0;i<d;i++){var m=i;for(var j=i+1;j<d;j++)if(Math.abs(M[j][i])>Math.abs(M[m][i]))m=j;"
"var t=M[i];M[i]=M[m];M[m]=t;if(!M[i][i])return null;"
"for(var j=0;j<d;j++)if(j!=i){var f=M[j][i]/M[i][i];for(var q=i;q<=d;q++)M[j][q]-=f*M[i][q]}}"
"var c=[];for(var i=0;i<d;i++)c.push(M[i][d]/M[i][i]);"
"if(!(c[1]>0))return null;return c}"
"var CAL={c:null},CALDIRTY=0;"
"try{window.CALAX=+(localStorage.getItem('mca_calax')||0);window.CALLN=+(localStorage.getItem('mca_calln')||0);window.CALTAB=+(localStorage.getItem('mca_caltab')||0)}"
"catch(e){window.CALAX=0;window.CALLN=0;window.CALTAB=0}"
"function calSN(){return [L('способ','method'),L('до вершины','before peak'),L('после вершины','after peak'),'L','G',L('плато','flat top'),L('кодов на канал','codes per channel'),L('полярность','polarity'),L('частота','rate')]}"
"function calSig(){var p=PC||{};return [p.algo,p.int_rise,p.int_fall,p.trap_L,p.trap_G,p.flat_avg,p.cpc,p.polarity,window.FHZ||0].join(',')}"
"function calOk(){return !!(CAL.c&&CAL.sig==calSig())}"
"function calE(x){return calEv(CAL.c,x)}"
"function calCh(E){return calChv(CAL.c,E)}"
"function calMsg(t,bad){var e=document.getElementById('cmsg');if(e)e.innerHTML=t?'<span style=color:'+(bad?'#ff8a82':'#7ee081')+'>'+t+'</span>':''}"
"function calLoad(){fetch('/calib').then(function(r){return r.json()}).then(function(j){if(j&&j.c&&j.c.length>=2&&!CALDIRTY)CAL=j;calShow();calManRender()}).catch(function(){})}"
"function calStore(v){try{localStorage.setItem(v[0],v[1])}catch(e){}}"
"function calSet(Ls,name){var c=calPoly(Ls);"
"if(!c){calMsg(L('шкала не строится: проверьте линии','the scale cannot be built: check the lines'),1);return}"
"CAL={c:c,sig:calSig(),iso:name,t:Math.floor(Date.now()/1000),"
"lines:Ls.map(function(l){return [l.E,+l.ch.toFixed(2),+l.fw.toFixed(2)]})};"
"CALDIRTY=1;"
"calMsg(L('калибровка готова, но ещё не сохранена: проверьте линии и нажмите «Сохранить в настройки»',"
"'the calibration is ready but not saved yet: check the lines and press “Save to settings”'),0);"
"calShow();calManRender();poll()}"
"function calAuto(){var a=window.LASTSPEC;"
"if(!a||!a.length){calMsg(L('спектра ещё нет','no spectrum yet'),1);return}"
"var tot=0;for(var i=0;i<a.length;i++)tot+=a[i];"
"if(tot<50000){calMsg(L('в спектре мало событий (','too few events in the spectrum (')+tot+L(') - наберите хотя бы 50 тысяч',') - acquire at least 50 thousand'),1);return}"
"var iso=CALISO[document.getElementById('cis').value],Ls=calFind(a,iso[1]);"
"if(!Ls.length){calMsg(L('линии ','lines of ')+iso[0]+L(' не найдены: наберите спектр подольше или выберите другой изотоп',' not found: acquire longer or choose another isotope'),1);return}"
"calSet(Ls,iso[0])}"
"function calSaveBtn(){if(!CAL.c){calMsg(L('сохранять нечего: сначала «Откалибровать»','nothing to save: “Calibrate” first'),1);return}"
"if(!CALDIRTY){calMsg(L('эта калибровка уже сохранена в приборе','this calibration is already saved in the device'),0);return}"
"if(!calOk()){calMsg(L('настройки сменились после калибровки - нажмите «Откалибровать» заново','the settings changed after the calibration - press “Calibrate” again'),1);return}"
"fetch('/calib',{method:'POST',body:JSON.stringify(CAL)}).then(function(r){return r.json()}).then(function(j){"
"if(j.ok){CALDIRTY=0;calMsg(L('калибровка сохранена в приборе','the calibration is saved in the device'),0)}"
"else calMsg(L('калибровка не сохранилась в приборе','the calibration was not saved in the device'),1);calShow()}).catch(function(){"
"calMsg(L('нет связи с прибором - калибровка не сохранена','no connection with the device - the calibration is not saved'),1)})}"
"function calLines(){if(!window.CALLN&&!calOk()){calMsg(L('нет калибровки для этих настроек: сначала «Откалибровать»','no calibration for these settings: “Calibrate” first'),1);return}"
"window.CALLN=window.CALLN?0:1;calStore(['mca_calln',window.CALLN]);calShow();specRender()}"
"function calTab(){window.CALTAB=window.CALTAB?0:1;calStore(['mca_caltab',window.CALTAB]);calShow();calManRender()}"
"function calManRender(){var m=document.getElementById('cman');if(!m)return;"
"if(!window.CALTAB){m.innerHTML='';return}"
"var c=CAL.c||[],h='<div class=grp style=margin-top:8px><span class=lbl>'+"
"L('коэффициенты вручную, E = c0 + c1&middot;k + &hellip; + c4&middot;k<sup>4</sup>, кэВ','coefficients by hand, E = c0 + c1&middot;k + &hellip; + c4&middot;k<sup>4</sup>, keV')+'</span>';"
"for(var i=0;i<5;i++)h+='<span style=white-space:nowrap><span class=lbl>c'+i+'</span> <input id=cc'+i+' type=text inputmode=decimal style=width:128px value='+(c[i]===undefined||(i>1&&!c[i])?'':(i==0?+c[i].toFixed(4):(i==1?+c[i].toPrecision(8):c[i].toExponential(6))))+'></span>';"
"h+='<button class=\\\"btn sm\\\" onclick=calManual()>'+L('Применить','Apply')+'</button></div>';m.innerHTML=h+'<div id=cpts></div>';calPtRender()}"
"var CALPT=[null,null,null,null],CALPA=-1;"
"try{var q0=JSON.parse(localStorage.getItem('mca_calpt')||'null');if(q0&&q0.length==4)CALPT=q0}catch(e){}"
"function calPtSave(){calStore(['mca_calpt',JSON.stringify(CALPT)])}"
"function calPtSync(){for(var i=0;i<4;i++){var e=document.getElementById('cpe'+i);if(e&&CALPT[i])CALPT[i].E=e.value.replace(/[^0-9.,eE+-]/g,'')}}"
"function calPtIn(){calPtSync();calPtSave()}"
"function calPtRender(){var m=document.getElementById('cpts');if(!m)return;"
"var h='<div class=grp style=margin-top:8px><span class=lbl>'+L('по точкам: щёлкните по пику на спектре (или протяните по нему) - центр пика встанет в строку, выбранную кнопкой номера, или в первую свободную; впишите энергию линии',"
"'by points: click a peak on the spectrum (or drag over it) - the peak centroid goes to the row chosen with its number button or to the first free one; enter the line energy')+'</span></div>';"
"for(var i=0;i<4;i++){var p=CALPT[i];"
"h+='<div class=grp style=margin-top:4px><button class=\\\"btn sm'+(CALPA==i?' on':'')+'\\\" onclick=calPtSel('+i+') title=\\\"'+"
"L('следующий щелчок по спектру - в эту строку','the next click on the spectrum goes to this row')+'\\\">'+(i+1)+'</button> '+"
"'<span style=display:inline-block;min-width:190px>'+(p?L('канал ','channel ')+p.ch.toFixed(1)+(calOk()?' <span class=mut>(&asymp; '+calE(p.ch).toFixed(1)+L(' кэВ',' keV')+')</span>':''):"
"'<span class=mut>'+L('пусто','empty')+'</span>')+'</span> <span class=lbl>E, '+L('кэВ','keV')+'</span> '+"
"'<input id=cpe'+i+' type=text inputmode=decimal style=width:96px oninput=calPtIn()'+(p?' value='+(p.E||'\\\"\\\"'):' disabled')+'> '+"
"'<button class=\\\"btn sm\\\" onclick=calPtDel('+i+') title=\\\"'+L('очистить строку','clear the row')+'\\\">&times;</button></div>'}"
"h+='<div class=grp style=margin-top:4px><button class=\\\"btn sm\\\" onclick=calPts()>'+L('Построить по точкам','Build from points')+'</button> '+"
"'<span class=mut>'+L('1 точка - прямая через ноль, 2-3 - прямая, 4 - парабола','1 point - a line through zero, 2-3 - a line, 4 - a parabola')+'</span></div>';"
"m.innerHTML=h}"
"function calPtSel(i){calPtSync();CALPA=CALPA==i?-1:i;calPtRender()}"
"function calPtDel(i){calPtSync();CALPT[i]=null;if(CALPA==i)CALPA=-1;calPtSave();calPtRender();specRender()}"
"function calPtAdd(ch,fw){if(!window.CALTAB)return 0;calPtSync();"
"var i=CALPA>=0?CALPA:CALPT.indexOf(null);"
"if(i<0){calMsg(L('все 4 строки заняты: выберите строку кнопкой номера или очистите её','all 4 rows are taken: choose a row with its number button or clear it'),1);return 0}"
"CALPT[i]={ch:+ch.toFixed(2),fw:+fw.toFixed(2),E:CALPT[i]&&CALPT[i].E||''};CALPA=-1;calPtSave();calPtRender();calMsg('');"
"var e=document.getElementById('cpe'+i);if(e&&!e.value)try{e.focus({preventScroll:true})}catch(_){}return 1}"
"function calPts(){calPtSync();var P=[],bad=0;"
"CALPT.forEach(function(p,i){if(!p||bad)return;var E=+String(p.E).replace(',','.');"
"if(String(p.E).trim()===''||!(E>0)||!isFinite(E)){bad=i+1;return}P.push({ch:p.ch,fw:p.fw,E:E})});"
"if(bad){calMsg(L('строка ','row ')+bad+L(': впишите энергию линии в кэВ',': enter the line energy in keV'),1);return}"
"if(!P.length){calMsg(L('точек нет: щёлкните по пику на спектре','no points: click a peak on the spectrum'),1);return}"
"P.sort(function(a,b){return a.ch-b.ch});"
"for(var i=1;i<P.length;i++){if(P[i].ch-P[i-1].ch<1){calMsg(L('две строки на одном пике','two rows on the same peak'),1);return}"
"if(!(P[i].E>P[i-1].E)){calMsg(L('энергии должны расти вместе с номером канала','the energies must grow with the channel number'),1);return}}"
"var c=calPoly(P);if(!c){calMsg(L('шкала не строится: проверьте точки','the scale cannot be built: check the points'),1);return}"
"var n=(window.LASTSPEC&&LASTSPEC.length)||(PC&&PC.nch)||2048;"
"for(var k=0;k<=64;k++)if(!(calDv(c,k*(n-1)/64)>0)){calMsg(L('по этим точкам шкала не растёт на всём спектре: проверьте энергии','with these points the scale does not grow over the whole spectrum: check the energies'),1);return}"
"calSet(P,L('по точкам','by points'))}"
"function calManual(){var c=[];"
"for(var i=0;i<5;i++){var v=document.getElementById('cc'+i).value.trim().replace(',','.');c.push(v===''?0:+v)}"
"if(c.some(function(v){return !isFinite(v)})){calMsg(L('коэффициенты - это числа','the coefficients must be numbers'),1);return}"
"while(c.length>2&&c[c.length-1]==0)c.pop();"
"var n=(window.LASTSPEC&&LASTSPEC.length)||(PC&&PC.nch)||2048;"
"for(var k=0;k<=64;k++)if(!(calDv(c,k*(n-1)/64)>0)){calMsg(L('шкала должна расти с номером канала на всём спектре (каналы 0-','the scale must grow with the channel number over the whole spectrum (channels 0-')+(n-1)+')',1);return}"
"CAL={c:c,sig:calSig(),iso:L('вручную','by hand'),t:Math.floor(Date.now()/1000),lines:[]};CALDIRTY=1;"
"calMsg(L('коэффициенты применены, но ещё не сохранены: «Сохранить в настройки»','the coefficients are applied but not saved yet: “Save to settings”'),0);"
"calShow();calManRender();poll()}"
"function calSum(){var e=document.getElementById('csum');if(!e)return;"
"var s=CAL.c?(calOk()?L('энергия: ','energy: ')+(CAL.iso||''):L('калибровка по энергии при других настройках','energy calibration made with other settings')):L('нет калибровки по энергии','no energy calibration');"
"if(CAL.c&&CALDIRTY)s+=L(' (не сохранена)',' (not saved)');"
"if(window.DOSE&&DOSE.det!=null&&window.KDET)s+=' · '+L('доза: ','dose: ')+'NaI '+KDET[DOSE.det].join('×')+', k '+(+DOSE.k).toFixed(3);"
"e.textContent=s}"
"function calAx(v){window.CALAX=v;calStore(['mca_calax',v]);calShow();poll()}"
"function calShow(){var o=document.getElementById('cout'),st=document.getElementById('cst');if(!o||!st)return;"
"for(var i=0;i<2;i++){var b=document.getElementById('cax'+i);if(b)b.className=i==(window.CALAX&&calOk()?1:0)?'on':''}"
"var b1=document.getElementById('cax1');if(b1){b1.disabled=!calOk();b1.title=calOk()?'':L('нет годной калибровки','no valid calibration')}"
"var bl=document.getElementById('cbl');if(bl){bl.innerHTML=window.CALLN?L('Скрыть линии калибровки','Hide calibration lines'):L('Показать линии калибровки','Show calibration lines');"
"bl.className='btn sm'+(window.CALLN?' on':'')}"
"var bt=document.getElementById('cbt');if(bt)bt.innerHTML=(window.CALTAB?'&#9662; ':'&#9656; ')+L('Калибровка','Calibration');"
"var cb=document.getElementById('calbody');if(cb)cb.hidden=!window.CALTAB;calSum();"
"var bs=document.getElementById('cbs');if(bs)bs.disabled=!(CAL.c&&CALDIRTY&&calOk());"
"if(!CAL.c){st.innerHTML='<span class=mut>'+L('калибровки нет: выберите изотоп и «Откалибровать»','no calibration: choose the isotope and “Calibrate”')+'</span>';o.innerHTML='';return}"
"var l6=(CAL.lines||[]).filter(function(l){return Math.abs(l[0]-609.31)<1||Math.abs(l[0]-661.66)<1})[0],c=CAL.c,s1='';"
"if(l6){var fk=calEv(c,l6[1]+l6[2]/2)-calEv(c,l6[1]-l6[2]/2);s1=', '+Math.round(l6[0])+L(' кэВ: FWHM ',' keV: FWHM ')+fk.toFixed(1)+L(' кэВ (',' keV (')+(100*fk/l6[0]).toFixed(2)+' %)'}"
"var s=(CAL.iso||'')+((CAL.lines||[]).length?', '+CAL.lines.length+L(' линии',' lines'):'')+s1+(CAL.t?' &nbsp;<span class=mut>'+new Date(CAL.t*1000).toLocaleString()+'</span>':'');"
"if(CALDIRTY)s+=' &nbsp;<b style=color:#f0c45a>'+L('не сохранена','not saved')+'</b>';"
"if(!calOk()){var a=(CAL.sig||'').split(','),b=calSig().split(','),d=[];"
"var sn=calSN();for(var i=0;i<sn.length;i++)if(a[i]!=b[i])d.push(sn[i]);"
"s+='<br><b style=color:#ff8a82>'+L('калибровка набрана при других настройках','the calibration was made with other settings')+"
"(d.length?' ('+L('изменились: ','changed: ')+d.join(', ')+')':'')+"
"L(' - шкала в кэВ не действует; нажмите «Откалибровать»',' - the keV scale is not applied; press “Calibrate”')+'</b>'}"
"st.innerHTML=s;"
"if(!window.CALTAB){o.innerHTML='';return}"
"var t='E = '+c[0].toFixed(2);"
"for(var i=1;i<c.length;i++)if(c[i]||i==1)t+=' '+(c[i]<0?'&minus; ':'+ ')+Math.abs(c[i]).toPrecision(i==1?6:4)+'&middot;k'+(i>1?'<sup>'+i+'</sup>':'');"
"t+=L(' кэВ (k - канал); ноль шкалы ',' keV (k - channel); scale zero ')+c[0].toFixed(1)+L(' кэВ',' keV');"
"if(CAL.lines&&CAL.lines.length){t+='<table style=margin-top:6px><tr><th>'+L('линия, кэВ','line, keV')+'</th><th>'+L('канал','channel')+"
"'</th><th>FWHM, '+L('кэВ','keV')+'</th><th>'+L('разрешение','resolution')+'</th><th>'+L('отклонение, кэВ','deviation, keV')+'</th></tr>';"
"CAL.lines.forEach(function(l){var fk=calEv(c,l[1]+l[2]/2)-calEv(c,l[1]-l[2]/2);"
"t+='<tr><td>'+l[0].toFixed(2)+'</td><td>'+l[1].toFixed(1)+'</td><td>'+fk.toFixed(1)+'</td><td>'+(100*fk/l[0]).toFixed(2)+"
"' %</td><td>'+(l[0]-calEv(c,l[1])).toFixed(1)+'</td></tr>'});t+='</table>'}"
"o.innerHTML=t}"
"calLoad();"
/* СПЕКТР: линии калибровки на графике (использованные - ярко, остальные
   линии того же изотопа - бледно), выделение пика мышью (щелчок - пик
   вокруг точки: максимум в ±4 % и подгонка, участок центр ±1.5 FWHM;
   протяжка - свой участок; двойной щелчок - сброс), итог измерителя и
   показания курсора прямо на графике. */
"var ISOALL={'Ra-226':[186.21,241.99,295.22,351.93,609.31,768.36,934.06,1120.29,1238.11,1377.67,1764.49,2204.21],"
"'Cs-137':[32.19,661.66],'Cs-137 + K-40':[32.19,661.66,1460.82],'Th-232':[238.63,338.32,583.19,727.33,911.20,968.97,2614.51],"
"'Co-60':[1173.23,1332.49]};"
"var SSEL=null,SHOV=null;"
"function spX(c,W,n){return c*W/n}"
"function spCh(x,W,n){return x*n/W}"
"function specRender(){var a=window.LASTSPEC;if(!a||!a.length||isScope())return;"
"draw(a,document.getElementById('lg').checked);"
"var mx=0,tot=0,pk=0;for(var i=0;i<a.length;i++){tot+=a[i];if(a[i]>mx){mx=a[i];pk=i}}"
"var s=L('всего в спектре: ','total in spectrum: ')+tot+L('   максимум ','   max ')+mx+L(' в канале ',' in channel ')+pk+"
"L('   каналов: ','   channels: ')+a.length;"
"s+=peakInfo(a);"
"specOver(a);"
"drawPk(a.length,cv.width,cv.height);"
"document.getElementById('sc').innerHTML=s}"
"function specOver(a){var W=cv.width,H=cv.height,n=a.length;cx.save();cx.font='11px ui-monospace,monospace';"
"if(window.CALLN&&calOk()){var used=(CAL.lines||[]).map(function(l){return l[0]}),all=ISOALL[CAL.iso]||[],occ=[];"
"var marks=(CAL.lines||[]).map(function(l){return [l[0],l[1],1]})"
".concat(all.filter(function(e){return used.every(function(u){return Math.abs(u-e)>0.5})}).map(function(e){return [e,calCh(e),0]}));"
"marks.forEach(function(m){var x=spX(m[1],W,n);if(!(x>=0&&x<=W))return;"
"cx.strokeStyle=m[2]?'rgba(240,196,90,.75)':'rgba(240,196,90,.28)';cx.setLineDash(m[2]?[5,3]:[2,4]);cx.lineWidth=1;"
"cx.beginPath();cx.moveTo(x,TOPM+14);cx.lineTo(x,H-BOTM);cx.stroke();cx.setLineDash([]);"
"var t=String(Math.round(m[0])),tw=cx.measureText(t).width,tx=Math.min(Math.max(x,tw/2+2),W-tw/2-2),x0=tx-tw/2-3,x1=tx+tw/2+3;"
"if(occ.some(function(o){return x1>o[0]&&x0<o[1]}))return;occ.push([x0,x1]);"
"cx.fillStyle=m[2]?'#f0c45a':'rgba(240,196,90,.55)';cx.textAlign='center';cx.fillText(t,tx,TOPM+11)});cx.textAlign='left'}"
"if(window.CALTAB&&window.CALPT)CALPT.forEach(function(p,i){if(!p)return;var x=spX(p.ch,W,n);if(!(x>=0&&x<=W))return;"
"cx.strokeStyle='rgba(52,211,192,.7)';cx.setLineDash([3,3]);cx.lineWidth=1;cx.beginPath();cx.moveTo(x,TOPM+50);cx.lineTo(x,H-BOTM);cx.stroke();cx.setLineDash([]);"
"cx.fillStyle='#34d3c0';cx.beginPath();cx.arc(x,TOPM+44,7,0,2*Math.PI);cx.fill();cx.fillStyle='#0c0f0b';cx.textAlign='center';cx.fillText(String(i+1),x,TOPM+48);cx.textAlign='left'});"
"if(SSEL){var x0=spX(Math.min(SSEL[0],SSEL[1]),W,n),x1=spX(Math.max(SSEL[0],SSEL[1]),W,n);"
"cx.fillStyle='rgba(240,196,90,.12)';cx.fillRect(x0,TOPM,x1-x0,plotH(H))}"
"var A=+document.getElementById('pka').value|0,B=+document.getElementById('pkb').value|0;"
"if(B>A+4){var xa=spX(A,W,n),xb=spX(B,W,n);cx.fillStyle='rgba(52,211,192,.07)';cx.fillRect(xa,TOPM,xb-xa,plotH(H));"
"var P=window.PKINFO;if(P){var t=calOk()?calE(P.cen).toFixed(1)+L(' кэВ  FWHM ',' keV  FWHM ')+P.fk.toFixed(1)+L(' кэВ  ',' keV  ')+P.rk.toFixed(2)+' %':"
"L('кан ','ch ')+P.cen.toFixed(1)+'  FWHM '+P.fw.toFixed(1)+L(' кан  ',' ch  ')+P.res.toFixed(2)+' %';"
"var tw=cx.measureText(t).width+12,xc=spX(P.cen,W,n),bx=Math.min(Math.max(xc-tw/2,2),W-tw-2);"
"cx.fillStyle='rgba(12,15,11,.88)';cx.fillRect(bx,TOPM+18,tw,18);cx.strokeStyle='#34d3c0';cx.strokeRect(bx+.5,TOPM+18.5,tw-1,17);"
"cx.fillStyle='#e8efe2';cx.fillText(t,bx+6,TOPM+31)}}"
"if(SHOV!=null&&SHOV>=0&&SHOV<n){var xh=spX(SHOV,W,n);cx.strokeStyle='rgba(255,255,255,.25)';cx.beginPath();cx.moveTo(xh,TOPM);cx.lineTo(xh,H-BOTM);cx.stroke();"
"var t=(calOk()?calE(SHOV).toFixed(1)+L(' кэВ',' keV')+' | ':'')+L('канал ','channel ')+Math.round(SHOV)+' | '+a[Math.round(SHOV)];"
"var tw=cx.measureText(t).width;cx.fillStyle='rgba(12,15,11,.85)';cx.fillRect(W-tw-12,H-BOTM-20,tw+8,16);"
"cx.fillStyle='#cfe0cc';cx.fillText(t,W-tw-8,H-BOTM-8)}"
"cx.restore()}"
"function specPick(c){var a=window.LASTSPEC;if(!a)return;var n=a.length,w=Math.max(3,Math.round(0.04*c)),pk=-1,m=-1;"
"for(var i=Math.max(1,Math.round(c)-w);i<=Math.min(n-2,Math.round(c)+w);i++){var v=a[i-1]+a[i]+a[i+1];if(v>m){m=v;pk=i}}"
"if(pk<0)return;var h=Math.max(6,0.08*pk),f=calFit(a,pk-h,pk+h);"
"if(f&&f.fw>2&&Math.abs(f.mu-pk)<0.5*f.fw+2){specSet(f.mu-1.5*f.fw,f.mu+1.5*f.fw);calPtAdd(f.mu,f.fw)}"
"else{specSet(pk-h,pk+h);if(window.CALTAB)calMsg(L('здесь пик не подогнался - точка не записана','no peak could be fitted here - the point is not recorded'),1)}}"
"function specSet(A,B){document.getElementById('pka').value=Math.max(0,Math.floor(Math.min(A,B)));"
"document.getElementById('pkb').value=Math.min(window.LASTSPEC.length-1,Math.ceil(Math.max(A,B)));specRender()}"
"(function(){var down=null;"
"cv.addEventListener('pointerdown',function(e){if(isScope()||!window.LASTSPEC)return;var n=window.LASTSPEC.length;"
"down={x:e.offsetX,c:spCh(e.offsetX,cv.width,n)};SSEL=null;try{cv.setPointerCapture(e.pointerId)}catch(_){}});"
"cv.addEventListener('pointermove',function(e){if(isScope()||!window.LASTSPEC)return;var n=window.LASTSPEC.length,c=spCh(e.offsetX,cv.width,n);"
"SHOV=c;if(down&&Math.abs(e.offsetX-down.x)>4)SSEL=[down.c,c];specRender()});"
"cv.addEventListener('pointerup',function(e){if(isScope()||!down)return;var n=window.LASTSPEC.length,c=spCh(e.offsetX,cv.width,n);"
"if(Math.abs(e.offsetX-down.x)>4){specSet(down.c,c);if(window.CALTAB){var f=calFit(window.LASTSPEC,Math.min(down.c,c),Math.max(down.c,c));"
"if(f&&f.fw>2)calPtAdd(f.mu,f.fw);else calMsg(L('здесь пик не подогнался - точка не записана','no peak could be fitted here - the point is not recorded'),1)}}else specPick(down.c);down=null;SSEL=null;specRender()});"
"cv.addEventListener('pointerleave',function(){if(isScope())return;SHOV=null;if(!down)specRender()});"
"cv.addEventListener('dblclick',function(){if(isScope())return;document.getElementById('pka').value=0;document.getElementById('pkb').value=0;specRender()})})();"
/* ПРОБА KCl. Ядро (kclcore.js): модель Монте-Карло пика 1461 кэВ от банки
   KCl под торцом кристалла NaI (порт tools/dosemodel/mc_kcl.py) и подгонка
   чистого пика по фону и пробе; tools/check_kcl.py. */
"var KXS=[[0.003,0.02459,700.3,0],[0.00347168,0.02851,481.7,0],[0.00401752,0.03305,331.3,0],[0.00464919,0.03778,631.4,0],[0.00538016,0.0428,694.7,0],[0.00622607,0.0481,480.7,0],[0.00720498,0.05363,327.6,0],[0.0083378,0.05961,223,0],[0.00964872,0.06572,151.2,0],[0.0111658,0.07151,101.9,0],[0.0129213,0.07748,68.54,0],[0.0149529,0.08395,46.09,0],[0.0173039,0.08951,30.77,0],[0.0200245,0.09467,20.55,0],[0.0231729,0.09942,13.63,0],[0.0268164,0.1037,9.051,0],[0.0286,0.1055,7.557,0],[0.0310326,0.1075,6.005,0],[0.0331,0.109,5.002,0],[0.03318,0.109,29.74,0],[0.0359118,0.1107,24.19,0],[0.0415581,0.1133,16.49,0],[0.0480921,0.1152,11.18,0],[0.0556535,0.1165,7.513,0],[0.0644038,0.117,5.025,0],[0.0745298,0.1168,3.349,0],[0.0862479,0.1159,2.223,0],[0.0998084,0.1144,1.47,0],[0.115501,0.1123,0.9699,0],[0.133661,0.1096,0.6385,0],[0.154676,0.1064,0.4209,0],[0.178995,0.1029,0.277,0],[0.207138,0.09908,0.1828,0],[0.239706,0.09506,0.121,0],[0.277394,0.09087,0.08044,0],[0.321008,0.08661,0.05384,0],[0.371479,0.08229,0.03634,0],[0.429886,0.07796,0.02475,0],[0.497475,0.07366,0.01705,0],[0.512,0.07282,0.01588,0],[0.575692,0.06941,0.01192,0],[0.666206,0.06522,0.008419,0],[0.770952,0.06113,0.006053,0],[0.892166,0.05717,0.004457,0],[1.0221,0.05355,0.003322,0],[1.03244,0.05329,0.00325,1.062e-08],[1.19477,0.04957,0.002434,8.079e-05],[1.38262,0.046,0.00186,0.000435],[1.461,0.0447,0.001683,0.00064],[1.6,0.04257,0.001429,0.001073]];"
"var KCL_MU=0.050891, KCL_G=16.63*0.1066, NAI_RHO=3.67, K40_E=1.46082;"
"var KDET=[[25,25],[30,40],[30,70],[40,40],[63,63],[80,80]];"
"function kRng(seed){var a=seed>>>0;return function(){a=(a+0x6D2B79F5)>>>0;var t=a;t=Math.imul(t^(t>>>15),t|1);t^=t+Math.imul(t^(t>>>7),t|61);return((t^(t>>>14))>>>0)/4294967296}}"
"function kxs(E){var T=KXS,n=T.length,i=0;if(E<=T[0][0])i=0;else if(E>=T[n-1][0])i=n-2;else{while(T[i+1][0]<E)i++}"
"var a=T[i],b=T[i+1],f=Math.log(E/a[0])/Math.log(b[0]/a[0]),r=[];"
"for(var k=1;k<4;k++){var y0=a[k],y1=b[k];r.push(y0>0&&y1>0?Math.exp(Math.log(y0)+f*Math.log(y1/y0)):(f<0.5?y0:y1))}"
"return r}"
"function kCyl(px,py,pz,dx,dy,dz,R,h2){var t1,t2,a=dx*dx+dy*dy,c=px*px+py*py-R*R;"
"if(a<1e-12){if(c>0)return null;t1=-1e30;t2=1e30}else{var b=2*(px*dx+py*dy),D=b*b-4*a*c;if(D<0)return null;var q=Math.sqrt(D);t1=(-b-q)/(2*a);t2=(-b+q)/(2*a)}"
"var z1,z2;if(Math.abs(dz)<1e-12){if(Math.abs(pz)>h2)return null;z1=-1e30;z2=1e30}else{var za=(-h2-pz)/dz,zb=(h2-pz)/dz;z1=Math.min(za,zb);z2=Math.max(za,zb)}"
"var ti=Math.max(t1,z1),to=Math.min(t2,z2);return ti<to?[ti,to]:null}"
"function kIso(rnd){var c=2*rnd()-1,f=2*Math.PI*rnd(),s=Math.sqrt(1-c*c);return [s*Math.cos(f),s*Math.sin(f),c]}"
"function kKN(k,rnd){var e0=1/(1+2*k);for(;;){var a1=-Math.log(e0),a2=0.5*(1-e0*e0),e;"
"if(rnd()<a1/(a1+a2))e=Math.exp(-a1*rnd());else e=Math.sqrt(e0*e0+(1-e0*e0)*rnd());"
"var t=(1-e)/(k*e),g=1-e*t*(2-t)/(1+e*e);if(rnd()<=g)return e}}"
"function kRot(d,ct,rnd){var f=2*Math.PI*rnd(),st=Math.sqrt(Math.max(0,1-ct*ct)),ux=d[0],uy=d[1],uz=d[2],cf=Math.cos(f),sf=Math.sin(f),o;"
"if(Math.abs(uz)>0.99999)o=[st*cf,st*sf,(uz>0?1:-1)*ct];"
"else{var s=Math.sqrt(1-uz*uz);o=[st*(ux*uz*cf-uy*sf)/s+ux*ct,st*(uy*uz*cf+ux*sf)/s+uy*ct,-st*cf*s+uz*ct]}"
"var m=Math.hypot(o[0],o[1],o[2]);return [o[0]/m,o[1]/m,o[2]/m]}"
"function kTrack(E0,p,d,R,h2,rnd){var st=[[E0,p,d]],dep=0;"
"while(st.length){var q=st.pop(),E=q[0],P=q[1].slice(),D=q[2];"
"for(;;){var iv=kCyl(P[0],P[1],P[2],D[0],D[1],D[2],R,h2);if(!iv||iv[1]<=0)break;"
"var x=kxs(E),mi=x[0]*NAI_RHO,mp=x[1]*NAI_RHO,mq=(E>1.022?x[2]:0)*NAI_RHO,mt=mi+mp+mq,s=-Math.log(rnd())/mt;"
"if(s>=iv[1])break;P=[P[0]+D[0]*s,P[1]+D[1]*s,P[2]+D[2]*s];var r=rnd()*mt;"
"if(r<mp){if(E>0.03317&&rnd()<0.83*0.88){dep+=E-0.0286;st.push([0.0286,P,kIso(rnd)])}else dep+=E;break}"
"if(r<mp+mq){dep+=E-2*0.51099895;var u=kIso(rnd);st.push([0.51099895,P,u]);st.push([0.51099895,P,[-u[0],-u[1],-u[2]]]);break}"
"var k=E/0.51099895,e=kKN(k,rnd),ct=1-(1-e)/(k*e);dep+=E*(1-e);E*=e;D=kRot(D,Math.max(-1,Math.min(1,ct)),rnd);"
"if(E<0.003){dep+=E;break}}}"
"return dep}"
"function kclEff(o,n,rnd,acc){var R=o.R,h2=o.H/2,z0=-(h2+o.gap),mu=KCL_MU*o.rho,s1=acc?acc.s1:0,s2=acc?acc.s2:0;"
"for(var i=0;i<n;i++){var r=o.rs*Math.sqrt(rnd()),f=2*Math.PI*rnd(),px=r*Math.cos(f),py=r*Math.sin(f),pz=z0-o.hs*rnd(),d=kIso(rnd);"
"if(d[2]<=0)continue;var iv=kCyl(px,py,pz,d[0],d[1],d[2],R,h2);if(!iv||iv[0]<=0)continue;"
"var Lp=(z0-pz)/d[2],a=d[0]*d[0]+d[1]*d[1];if(a>1e-12){var b=2*(px*d[0]+py*d[1]),c=px*px+py*py-o.rs*o.rs;Lp=Math.min(Lp,(-b+Math.sqrt(b*b-4*a*c))/(2*a))}"
"var w=Math.exp(-mu*Lp),t=iv[0]+1e-7,dep=kTrack(K40_E,[px+d[0]*t,py+d[1]*t,pz+d[2]*t],d,R,h2,rnd);"
"if(Math.abs(dep-K40_E)<0.002){s1+=w;s2+=w*w}}"
"return {s1:s1,s2:s2}}"
"function kclPeak(s,ts,b,tb,E){var n=Math.min(s.length,b.length),net=[],va=[];"
"for(var i=0;i<n;i++){net.push(s[i]/ts-b[i]/tb);va.push(Math.max(s[i],1)/(ts*ts)+Math.max(b[i],1)/(tb*tb))}"
"var sm=[];for(var i=0;i<n;i++){var t=0,c=0;for(var j=-4;j<=4;j++)if(i+j>=0&&i+j<n){t+=net[i+j];c++}sm.push(t/c)}"
"var pk=-1;for(var i=0;i<n;i++)if(E[i]>1461*0.85&&E[i]<1461*1.15&&(pk<0||sm[i]>sm[pk]))pk=i;"
"if(pk<0||!(sm[pk]>0))return null;"
"var l=pk,r=pk;while(l>0&&sm[l]>sm[pk]/2)l--;while(r<n-1&&sm[r]>sm[pk]/2)r++;"
"var s0=Math.max(2,(r-l)/2.3548),best=null;"
"for(var a=-10;a<=10;a++)for(var q=0;q<=16;q++){var mu=pk+a*s0/10,sg=s0*(0.6+0.05*q),x0=Math.round(mu-4*sg),x1=Math.round(mu+4.5*sg);"
"if(x0<0||x1>=n)continue;var M=[[0,0,0],[0,0,0],[0,0,0]],V=[0,0,0];"
"for(var i=x0;i<=x1;i++){var g=Math.exp(-0.5*Math.pow((i-mu)/sg,2))/(sg*2.5066283),f=[g,1,(i-mu)/sg],w=1/va[i];"
"for(var u=0;u<3;u++){V[u]+=w*f[u]*net[i];for(var v=0;v<3;v++)M[u][v]+=w*f[u]*f[v]}}"
"var C=kInv3(M);if(!C)continue;var P=[0,0,0];for(var u=0;u<3;u++)for(var v=0;v<3;v++)P[u]+=C[u][v]*V[v];"
"var x2=0;for(var i=x0;i<=x1;i++){var g=Math.exp(-0.5*Math.pow((i-mu)/sg,2))/(sg*2.5066283),y=P[0]*g+P[1]+P[2]*(i-mu)/sg-net[i];x2+=y*y/va[i]}"
"x2/=Math.max(1,x1-x0+1-5);if(!best||x2<best.x2)best={x2:x2,A:P[0],eA:Math.sqrt(C[0][0]),mu:mu,sg:sg}}"
"if(!best)return null;var k=Math.floor(best.mu),fr=best.mu-k,Ec=E[k]+(E[k+1]-E[k])*fr,de=E[k+1]-E[k];"
"return {area:best.A,err:best.eA*Math.sqrt(Math.max(1,best.x2)),E:Ec,fw:2.3548*best.sg*de,chi2:best.x2}}"
"function kInv3(m){var a=m[0][0],b=m[0][1],c=m[0][2],d=m[1][0],e=m[1][1],f=m[1][2],g=m[2][0],h=m[2][1],i=m[2][2],"
"A=e*i-f*h,B=-(d*i-f*g),C=d*h-e*g,D=a*A+b*B+c*C;if(!D)return null;"
"return [[A/D,-(b*i-c*h)/D,(b*f-c*e)/D],[B/D,(a*i-c*g)/D,-(a*f-c*d)/D],[C/D,-(a*h-b*g)/D,(a*e-b*d)/D]]}"
/* конец ядра пробы KCl */
"var KCLR=null,KCLF=['kms','kdi','kht','kgp'];"
"function kclMsg(t,bad){var e=document.getElementById('kmsg');if(e)e.innerHTML=t?'<span style=color:'+(bad?'#ff8a82':'#7ee081')+'>'+t+'</span>':''}"
"function kclIn(){var o={};KCLF.forEach(function(id){var e=document.getElementById(id);if(e){o[id]=+e.value.trim().replace(',','.');calStore(['mca_'+id,e.value])}});return o}"
"function kclBgGet(){try{return JSON.parse(localStorage.getItem('mca_kclbg')||'null')}catch(e){return null}}"
"function kclBgShow(){var b=kclBgGet(),e=document.getElementById('kbg');if(!e)return;"
"e.textContent=b?L('фон: ','background: ')+hms(b.ms/1000)+', '+b.n+L(' событий',' events'):L('фон не запомнен','no background stored')}"
"function kclInit(){KCLF.forEach(function(id){try{var v=localStorage.getItem('mca_'+id),e=document.getElementById(id);if(v!=null&&e)e.value=v}catch(_){}});kclBgShow()}"
"function kclBg(){var a=window.LASTSPEC;"
"if(!a||!a.length||!window.RUNMS){kclMsg(L('спектра ещё нет','no spectrum yet'),1);return}"
"if(RUNMS<300000){kclMsg(L('наберите фон хотя бы 5 минут','acquire the background for at least 5 minutes'),1);return}"
"var n=0;for(var i=0;i<a.length;i++)n+=a[i];"
"try{localStorage.setItem('mca_kclbg',JSON.stringify({d:Array.prototype.slice.call(a),ms:RUNMS,sig:calSig(),n:n,t:Date.now()}))}"
"catch(e){kclMsg(L('браузер не дал сохранить фон','the browser refused to store the background'),1);return}"
"kclBgShow();kclMsg(L('фон запомнен в браузере: поставьте пробу KCl, нажмите «Сброс» и наберите столько же','the background is stored in the browser: put the KCl sample, press “Reset” and acquire as long'),0)}"
"function kclRun(){var b=kclBgGet(),a=window.LASTSPEC,o=kclIn(),bk=document.getElementById('kbk');KCLR=null;if(bk)bk.disabled=true;"
"if(!b){kclMsg(L('сначала наберите фон пустого домика и нажмите «Запомнить фон»','first acquire the empty-castle background and press “Store background”'),1);return}"
"if(!a||!a.length||!window.RUNMS||RUNMS<300000){kclMsg(L('наберите пробу хотя бы 5 минут','acquire the sample for at least 5 minutes'),1);return}"
"if(b.sig!=calSig()){kclMsg(L('фон набран при других настройках - наберите его заново','the background was acquired with other settings - acquire it again'),1);return}"
"if(!calOk()){kclMsg(L('нужна калибровка по энергии для этих настроек','needs an energy calibration for these settings'),1);return}"
"if(!(o.kms>0&&o.kdi>0&&o.kht>0&&o.kgp>=0)){kclMsg(L('впишите массу соли, диаметр банки, высоту слоя и зазор','enter the salt mass, jar diameter, layer height and gap'),1);return}"
"var E=[];for(var i=0;i<a.length;i++)E.push(calE(i));"
"var p=kclPeak(a,RUNMS/1000,b.d,b.ms/1000,E);"
"if(!p||!(p.area>3*p.err)){kclMsg(L('пик K-40 не найден: наберите пробу подольше и проверьте калибровку','the K-40 peak is not found: acquire the sample longer and check the calibration'),1);return}"
"var di=(window.DOSE&&DOSE.det!=null)?DOSE.det:3,det=KDET[di],rs=o.kdi/20,hs=o.kht/10,"
"geo={R:det[0]/20,H:det[1]/10,rs:rs,hs:hs,rho:o.kms/(Math.PI*rs*rs*hs),gap:o.kgp/10},N=2000000,done=0,acc={s1:0,s2:0},rnd=kRng(12345);"
"(function step(){acc=kclEff(geo,200000,rnd,acc);done+=200000;"
"if(done<N){kclMsg(L('считаю модель: ','computing the model: ')+Math.round(100*done/N)+' %',0);setTimeout(step,0);return}"
"var src=o.kms*KCL_G,pr=acc.s1/N*src,pe=Math.sqrt(acc.s2)/N*src,k=pr/p.area,ek=k*Math.sqrt(Math.pow(pe/pr,2)+Math.pow(p.err/p.area,2)),"
"sh=100*(p.E/1460.82-1),t=L('пик K-40: ','K-40 peak: ')+'<b>'+p.area.toFixed(3)+' &plusmn; '+p.err.toFixed(3)+L(' имп/с',' cps')+'</b>'+"
"L(' на ',' at ')+p.E.toFixed(0)+L(' кэВ',' keV')+', FWHM '+p.fw.toFixed(0)+L(' кэВ',' keV')+'; '+"
"L('модель (','model (')+'NaI '+det.join('×')+L(', плотность соли ',', salt density ')+geo.rho.toFixed(2)+L(' г/см³): ',' g/cm³): ')+"
"pr.toFixed(3)+' &plusmn; '+pe.toFixed(3)+L(' имп/с',' cps')+' &rarr; <b>k = '+k.toFixed(3)+' &plusmn; '+ek.toFixed(3)+'</b>'+"
"L(' (статистика; сейчас k = ',' (statistics; now k = ')+(window.DOSE?(+DOSE.k).toFixed(3):'?')+')';"
"if(Math.abs(sh)>2)t+='<br><span style=color:#f0c45a>'+L('шкала энергии у 1461 кэВ сдвинута на ','the energy scale at 1461 keV is off by ')+sh.toFixed(1)+L(' % - перекалибруйте её, иначе доза тоже сдвинута',' % - recalibrate it, or the dose is off too')+'</span>';"
"if(k<0.5||k>2)t+='<br><span style=color:#ff8a82>'+L('k далеко от 1: проверьте блок детектирования, массу и размеры','k is far from 1: check the detector block, the mass and the dimensions')+'</span>';"
"KCLR={k:k};if(bk)bk.disabled=false;"
"document.getElementById('kmsg').innerHTML=t})()}"
"function kclSave(){if(!KCLR)return;doseK(KCLR.k.toFixed(4))}"
"kclInit();"
"</script></body></html>";

/* ---------------- обработчики ---------------- */
static esp_err_t h_root(httpd_req_t *r)
{
    httpd_resp_set_type(r, "text/html; charset=utf-8");
    return httpd_resp_send(r, PAGE, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t h_spectrum(httpd_req_t *r)
{
    mca_prof_web_begin(PROF_EP_SPEC);
    /* Числа собираются в буфер и отправляются крупными кусками.
     * Раньше на каждый канал приходился отдельный вызов отправки -
     * четыре тысячи записей в сокет на один запрос, и это заметно
     * отъедало процессор, мешая обработке потока. */
    static uint32_t val[512];
    static char     out[2048];
    size_t pos = 0;

    /* Показываем первые nch каналов - это длина шкалы. Прибор копит все
     * MCA_CHANNELS, поэтому переключение числа каналов ничего не теряет
     * и не сбрасывает: меняется только, докуда видно вправо. */
    mca_params_t p;
    mca_dsp_get_params(&p);
    size_t nch = (size_t)p.nch;
    if (nch > MCA_CHANNELS) nch = MCA_CHANNELS;

    httpd_resp_set_type(r, "application/json");
    httpd_resp_sendstr_chunk(r, "{\"d\":[");

    bool first = true;
    for (size_t off = 0; off < nch; off += 512) {
        size_t want = nch - off < 512 ? nch - off : 512;
        size_t got  = mca_dsp_get_spectrum(val, off, want);
        for (size_t i = 0; i < got; i++) {
            if (pos > sizeof(out) - 16) {
                httpd_resp_send_chunk(r, out, pos);
                pos = 0;
            }
            pos += snprintf(out + pos, sizeof(out) - pos, "%s%lu",
                            first ? "" : ",", (unsigned long)val[i]);
            first = false;
        }
    }
    if (pos) httpd_resp_send_chunk(r, out, pos);

    httpd_resp_sendstr_chunk(r, "]}");
    esp_err_t e = httpd_resp_send_chunk(r, NULL, 0);
    mca_prof_web_end(PROF_EP_SPEC);
    return e;
}

/* Настройки осциллографа из строки запроса - одни и те же для /scope и
 * для WebSocket: lvl - уровень синхронизации, n - сколько отсчётов нужно
 * странице, pre - сколько из них до момента синхронизации, amin/amax -
 * синхронизация по амплитуде (amax=0 - выкл). */
static void scope_apply(const char *q, size_t *want, size_t *pre)
{
    char v[12];
    if (httpd_query_key_value(q, "lvl", v, sizeof(v)) == ESP_OK)
        mca_diag_set_trig_level(atoi(v));
    if (httpd_query_key_value(q, "n", v, sizeof(v)) == ESP_OK) {
        int x = atoi(v);
        if (x > 0 && x <= DIAG_SCOPE_LEN) *want = (size_t)x;
    }
    if (httpd_query_key_value(q, "pre", v, sizeof(v)) == ESP_OK) {
        int x = atoi(v);
        if (x >= 0) *pre = (size_t)x;
    }
    int32_t lo = 0, hi = 0;
    if (httpd_query_key_value(q, "amin", v, sizeof(v)) == ESP_OK)
        lo = atoi(v);
    if (httpd_query_key_value(q, "amax", v, sizeof(v)) == ESP_OK)
        hi = atoi(v);
    mca_diag_set_amp_window(lo, hi);
}

static esp_err_t h_scope(httpd_req_t *r)
{
    /* Осциллограмма с синхронизацией по фронту. lvl в запросе -
     * уровень синхронизации, коды АЦП. Режим (авто / ждущий) задаётся
     * выбором режима прибора, а не этим запросом. */
    mca_prof_web_begin(PROF_EP_SCOPE);
    /* n - сколько отсчётов нужно странице, pre - сколько из них до
     * момента синхронизации. Отдаём только кусок под развёртку: время
     * ответа растёт с его длиной (см. LWIP_TCP_SND_BUF_DEFAULT). */
    char q[128];
    size_t want = DIAG_SCOPE_LEN, pre = DIAG_SCOPE_PRE;
    if (httpd_req_get_url_query_str(r, q, sizeof(q)) == ESP_OK)
        scope_apply(q, &want, &pre);
    /* прибор соберёт окно ровно под эту развёртку */
    mca_diag_set_scope_want(want, pre);

    /* Ответ двоичный, little-endian: семь int32 (tg, rise, age, lvl,
     * длина всего окна, высота импульса, отброшено фильтром амплитуды),
     * затем отсчёты по uint16. Втрое короче JSON, и прибору не нужно
     * форматировать десятки тысяч чисел через snprintf - от этого и
     * зависит, сколько раз в секунду обновляется картинка.
     *
     * Отсчёты уходят в сеть ПРЯМО ИЗ БУФЕРА СНИМКА, без копии. Раньше они
     * копировались в отдельный буфер 64 КБ в PSRAM, а снимок тоже лежал в
     * PSRAM: веб гонял кэш PSRAM, пока задача обработки писала туда
     * следующее окно, и на 20 МГц терялись чанки. Маска не нужна: в
     * камерный интерфейс заведены только 12 бит данных (см. adc_cap.c).
     * Пока ответ уходит, снимок занят - новые кадры не публикуются. */
    int32_t tg = -1, rise = 0, age = -1, amp = -1;
    uint32_t rej = 0;
    size_t full = 0;
    const uint16_t *d = NULL;
    size_t n = mca_diag_scope_acquire(want, pre, &d, &tg, &rise, &age, &full,
                                      &amp, &rej, NULL);

    const int32_t h[7] = { tg, rise, age, mca_diag_get_trig_level(),
                           (int32_t)full, amp, (int32_t)rej };
    httpd_resp_set_type(r, "application/octet-stream");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    esp_err_t e = httpd_resp_send_chunk(r, (const char *)h, sizeof(h));
    if (e == ESP_OK && n)
        e = httpd_resp_send_chunk(r, (const char *)d, n * sizeof(uint16_t));
    mca_diag_scope_release();
    if (e == ESP_OK) e = httpd_resp_send_chunk(r, NULL, 0);
    mca_prof_web_end(PROF_EP_SCOPE);
    return e;
}

/* /shape - фильтр формы: состояние набора эталона, гистограмма ширины,
 * эталонный импульс и последние импульсы в допуске и вне его. do=learn -
 * начать набор эталона, do=stop - прервать, do=clear - очистить
 * гистограмму. Каждый запрос продлевает расчёт меры на 10 с: пока страница
 * открыта, форма меряется и при выключенном фильтре.
 * Ответ двоичный, little-endian: 32 x int32 (порядок - ниже), затем
 * гистограмма SHAPE_HB x uint32, эталон и два примера по int16 (их длины -
 * в заголовке). */
static esp_err_t h_shape(httpd_req_t *r)
{
    char q[48], v[12];
    if (httpd_req_get_url_query_str(r, q, sizeof(q)) == ESP_OK &&
        httpd_query_key_value(q, "do", v, sizeof(v)) == ESP_OK) {
        if (!strcmp(v, "learn"))      mca_dsp_shape_learn(true);
        else if (!strcmp(v, "stop"))  mca_dsp_shape_learn(false);
        else if (!strcmp(v, "clear")) mca_dsp_shape_clear_hist();
    }
    mca_dsp_shape_watch();
    mca_shape_info_t si;
    mca_dsp_shape_info(&si);
    mca_params_t p;
    mca_dsp_get_params(&p);
    mca_stats_t st;
    mca_dsp_get_stats(&st);

    /* веб-сервер обрабатывает запросы по одному - буферы общие; примеры
     * держим до следующего нового, чтобы картинка не мигала */
    static uint32_t hist[SHAPE_HB];
    static mca_shape_wave_t wv;
    static mca_shape_ex_t ex[2], tmp;
    static int64_t ex_t[2];
    const int64_t now = esp_timer_get_time();
    for (int k = 0; k < 2; k++)
        if (mca_dsp_shape_example(k, &tmp)) { ex[k] = tmp; ex_t[k] = now; }
    mca_dsp_shape_hist(hist);
    mca_dsp_shape_wave(&wv);

    int32_t h[32] = {
        si.learn_n, si.learn_need, si.p10, si.p50, si.p90, si.win, si.active,
        p.shape_ref, p.shape_tol, p.shape_on, p.shape_win, p.shape_hz,
        (int32_t)adc_clk_get_freq(), (int32_t)st.shape_checked,
        (int32_t)st.skipped_shape, SHAPE_HB,
        wv.len, wv.pre, wv.n,
    };
    for (int k = 0; k < 2; k++) {       /* 19..23 и 24..28 */
        int32_t *o = &h[19 + 5 * k];
        o[0] = ex_t[k] ? ex[k].len : 0;
        o[1] = ex[k].pre;
        o[2] = ex[k].h;
        o[3] = ex[k].w10;
        o[4] = ex_t[k] ? (int32_t)((now - ex_t[k]) / 1000000) : -1;
    }
    h[29] = p.int_rise;
    httpd_resp_set_type(r, "application/octet-stream");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    esp_err_t e = httpd_resp_send_chunk(r, (const char *)h, sizeof(h));
    if (e == ESP_OK) e = httpd_resp_send_chunk(r, (const char *)hist, sizeof(hist));
    if (e == ESP_OK && wv.len > 0)
        e = httpd_resp_send_chunk(r, (const char *)wv.v, wv.len * sizeof(int16_t));
    for (int k = 0; k < 2 && e == ESP_OK; k++)
        if (h[19 + 5 * k] > 0)
            e = httpd_resp_send_chunk(r, (const char *)ex[k].v,
                                      ex[k].len * sizeof(int16_t));
    if (e == ESP_OK) e = httpd_resp_send_chunk(r, NULL, 0);
    return e;
}

/* /cps - только CPS, для опроса 10 раз в секунду со страницы спектра:
 * cpsf - быстрый CPS (окно от 100 мс до 1 с, при скачке счёта - 100 мс,
 * см. mca_dsp_tick_fast), cpsw - длина его окна, мс; cps - за прошлую
 * целую секунду, ev - всего событий, run - идёт набор. */
static esp_err_t h_cps(httpd_req_t *r)
{
    mca_stats_t s;
    mca_dsp_get_stats(&s);
    /* dh, dr - мощность дозы тем же быстрым окном: мкЗв/ч (H*(10)) и мкР/ч,
     * с поправкой k; dok - доза считается (есть калибровка для этих
     * настроек, mca_dose.h) */
    const float k = mca_dose_k();
    const bool dok = mca_dose_state() == MCA_DOSE_OK;
    char buf[160];
    const int n = snprintf(buf, sizeof(buf),
        "{\"cpsf\":%lu,\"cpsw\":%lu,\"cps\":%lu,\"ev\":%llu,\"run\":%d,"
        "\"dok\":%d,\"dh\":%.4g,\"dr\":%.4g}",
        (unsigned long)s.cps_fast, (unsigned long)s.cps_fast_ms,
        (unsigned long)s.cps,
        (unsigned long long)s.total_events, mca_spec_run ? 1 : 0, dok ? 1 : 0,
        dok ? k * MCA_DOSE_USVH(s.dose_h_fast, 1.0) : 0.0,
        dok ? k * MCA_DOSE_URH(s.dose_k_fast, 1.0) : 0.0);
    if (n < 0 || n >= (int)sizeof(buf)) return httpd_resp_send_500(r);
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    return httpd_resp_send(r, buf, n);
}

/* /dose - мощность дозы: ok, why (mca_dose_why_t), det - блок
 * детектирования (MCA_DOSE_DET_NAME), k - его поправка, s - время набора с
 * «Сброса», с; h0, r0 - средние с «Сброса» без поправки, мкЗв/ч и мкР/ч;
 * h, r - с поправкой; n - событий.
 * /dose?det=3 - выбрать блок; /dose?k=1.23 - записать поправку блока
 * (0.05 ... 20). Таблицы под новый блок - на следующем секундном тике. */
static esp_err_t h_dose(httpd_req_t *r)
{
    char q[48], v[24];
    int set = -1;
    if (httpd_req_get_url_query_str(r, q, sizeof(q)) == ESP_OK) {
        if (httpd_query_key_value(q, "det", v, sizeof(v)) == ESP_OK) {
            char *end;
            const long d = strtol(v, &end, 10);
            set = (end != v && mca_dose_set_det((int)d) == ESP_OK) ? 1 : 0;
        }
        if (httpd_query_key_value(q, "k", v, sizeof(v)) == ESP_OK) {
            char *end;
            const double k = strtod(v, &end);
            set = (end != v && mca_dose_set_k((float)k) == ESP_OK) ? 1 : 0;
        }
    }
    /* суммы и время набора - одним снимком: run_ms в статистике
     * обновляется раз в секунду, а суммы - на каждом чанке, и в окне 10 с
     * на странице это давало бы скачки до 10 % */
    double dh, dk;
    uint64_t ev, rn;
    mca_dsp_dose_snap(&dh, &dk, &ev, &rn);
    const double sec = rn / 1e9;
    const bool ok = mca_dose_state() == MCA_DOSE_OK;
    const double h0 = (ok && sec > 0) ? MCA_DOSE_USVH(dh, sec) : 0;
    const double r0 = (ok && sec > 0) ? MCA_DOSE_URH(dk, sec) : 0;
    const float k = mca_dose_k();
    /* hs, ks - суммы весов с «Сброса» без поправки, пЗв и пГр: по ним
     * страница считает мощность дозы за скользящие 10 с */
    char buf[320];
    const int n = snprintf(buf, sizeof(buf),
        "{\"ok\":%d,\"why\":%d,\"det\":%d,\"k\":%.5g,\"s\":%.4f,\"n\":%llu,"
        "\"h0\":%.5g,\"r0\":%.5g,\"h\":%.5g,\"r\":%.5g,\"hs\":%.9g,\"ks\":%.9g,\"set\":%d}",
        ok ? 1 : 0, (int)mca_dose_state(), mca_dose_det(), k, sec,
        (unsigned long long)ev, h0, r0, h0 * k, r0 * k, dh, dk, set);
    if (n < 0 || n >= (int)sizeof(buf)) return httpd_resp_send_500(r);
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    return httpd_resp_send(r, buf, n);
}

static esp_err_t h_stat(httpd_req_t *r)
{
    mca_prof_web_begin(PROF_EP_STAT);
    /* страница опрашивает /stat постоянно: первый же запрос значит, что
     * новая прошивка поднялась и доступна по сети - отката не будет */
    mca_ota_confirm();
    mca_stats_t s;
    mca_dsp_get_stats(&s);
    /* load - загрузка обработки: доля времени за прошлую секунду, когда
     * задача обработки работает с чанками (от получения чанка до конца
     * работы с ним), та же цифра, что в «Диагностике». Растёт вместе с
     * работой и подходит к 100 %, когда начинаются потери.
     * Не загрузка ядра 1 по FreeRTOS: та ещё включает ~3100 тактов на
     * засыпание и пробуждение задачи на каждом чанке (замер 2026-11-05,
     * 20 МГц: 76 % против 64 %), а под нагрузкой, когда чанк уже ждёт в
     * очереди, этих тактов нет - получение стоит 375. И не такты внутри
     * mca_dsp_process (s.load_pm, 57 %): без служебной работы вокруг. */
    mca_prof_t pf;
    mca_prof_get(&pf);
    char buf[600];
    int n = snprintf(buf, sizeof(buf),
        "{\"ev\":%llu,\"cps\":%lu,\"pile\":%llu,\"dead\":%llu,"
        "\"base\":%ld,\"lost\":%llu,\"over\":%llu,"
        "\"samp\":%llu,\"freq\":%lu,"
        "\"run\":%d,\"ms\":%lu,\"mode\":%d,\"load\":%lu,\"vec\":%d,"
        "\"srun\":%d,\"cap\":%d,\"tnow\":%lld,\"tsrc\":%d,"
        "\"shchk\":%llu,\"shrej\":%llu}",
        (unsigned long long)s.total_events, (unsigned long)s.cps,
        (unsigned long long)s.skipped_pileup,
        (unsigned long long)s.skipped_deadtime,
        (long)s.baseline, (unsigned long long)s.chunks_lost,
        (unsigned long long)s.overflow,
        (unsigned long long)s.samples_processed,
        (unsigned long)adc_clk_get_freq(),
        /* run - набор спектра, srun - осциллограф, cap - сам захват АЦП */
        mca_spec_run ? 1 : 0, (unsigned long)s.run_ms,
        (int)mca_mode, (unsigned long)pf.busy_pm, mca_dsp_vec_ok() ? 1 : 0,
        mca_scope_run ? 1 : 0, adc_cap_is_running() ? 1 : 0,
        /* часы прибора: мс Unix-времени (0 - не заданы) и откуда время */
        (long long)mca_time_now_ms(), (int)mca_time_source(),
        /* фильтр формы: проверено и отброшено с «Сброса» */
        (unsigned long long)s.shape_checked, (unsigned long long)s.skipped_shape);
    httpd_resp_set_type(r, "application/json");
    /* snprintf возвращает длину, которая ПОТРЕБОВАЛАСЬ БЫ. При нехватке
     * места это больше размера буфера, и отправка читала бы за его
     * границей. Ограничиваем. */
    if (n > (int)sizeof(buf) - 1) n = (int)sizeof(buf) - 1;
    esp_err_t e = httpd_resp_send(r, buf, n);
    mca_prof_web_end(PROF_EP_STAT);
    return e;
}

static bool q_int(const char *q, const char *key, int32_t *out)
{
    char v[16];
    if (httpd_query_key_value(q, key, v, sizeof(v)) == ESP_OK) {
        *out = atoi(v);
        return true;
    }
    return false;
}

static esp_err_t h_cfg(httpd_req_t *r)
{
    char q[1024];
    mca_params_t p, before;
    mca_dsp_get_params(&p);
    before = p;

    if (httpd_req_get_url_query_str(r, q, sizeof(q)) == ESP_OK) {
        int32_t v;
        if (q_int(q, "mode", &v) && v >= 0 && v <= MCA_MODE_MAX)
            mca_mode = (mca_mode_t)v;
        if (q_int(q, "freq", &v)) mca_cmd_freq_idx = v;
        /* эмуляция MCA на COM: скорость порта или 0 - выключить */
        if (q_int(q, "emu", &v)) mca_emu_set((uint32_t)v);

        q_int(q, "threshold",       &p.threshold);
        q_int(q, "cpc",             &p.cpc_milli);
        q_int(q, "nch",             &p.nch);
        q_int(q, "algo",            &p.algo);
        q_int(q, "polarity",        &p.polarity);
        q_int(q, "int_rise",        &p.int_rise);
        q_int(q, "int_fall",        &p.int_fall);
        q_int(q, "hysteresis",      &p.hysteresis);
        q_int(q, "trap_L",          &p.trap_L);
        q_int(q, "trap_G",          &p.trap_G);
        q_int(q, "rearm",           &p.rearm);
        q_int(q, "search",          &p.search);
        q_int(q, "baseline_shift",  &p.baseline_shift);
        q_int(q, "baseline_win",    &p.baseline_win);
        q_int(q, "flat_avg",        &p.flat_avg);
        q_int(q, "pileup_pre_pct",  &p.pileup_pre_pct);
        q_int(q, "pileup_post_pct", &p.pileup_post_pct);
        q_int(q, "shape_on",        &p.shape_on);
        q_int(q, "shape_tol",       &p.shape_tol);
        q_int(q, "shape_lmin",      &p.shape_lmin);
        q_int(q, "shape_lmax",      &p.shape_lmax);
        q_int(q, "shape_ln",        &p.shape_ln);
        mca_dsp_set_params(&p);
        mca_dsp_get_params(&p);

        /* В память прибора - только если настройки реально поменялись,
         * чтобы запросы режима и частоты не изнашивали флеш. */
        if (memcmp(&before, &p, sizeof(p)) != 0) mca_settings_save_params(&p);
    }

    char buf[900];
    int n = snprintf(buf, sizeof(buf),
        "{\"threshold\":%ld,\"cpc\":%ld,\"nch\":%ld,"
        "\"algo\":%ld,\"polarity\":%ld,\"int_rise\":%ld,\"int_fall\":%ld,"
        "\"hysteresis\":%ld,\"trap_L\":%ld,"
        "\"trap_G\":%ld,\"rearm\":%ld,\"search\":%ld,"
        "\"baseline_shift\":%ld,\"baseline_win\":%ld,"
        "\"flat_avg\":%ld,"
        "\"pileup_pre_pct\":%ld,"
        "\"pileup_post_pct\":%ld,\"shape_on\":%ld,\"shape_tol\":%ld,"
        "\"shape_lmin\":%ld,\"shape_lmax\":%ld,\"shape_ln\":%ld,"
        "\"shape_ref\":%ld,\"shape_win\":%ld,\"shape_hz\":%ld,"
        "\"emu\":%lu,\"fl\":[",
        (long)p.threshold, (long)p.cpc_milli, (long)p.nch,
        (long)p.algo, (long)p.polarity, (long)p.int_rise, (long)p.int_fall,
        (long)p.hysteresis, (long)p.trap_L,
        (long)p.trap_G, (long)p.rearm, (long)p.search,
        (long)p.baseline_shift, (long)p.baseline_win,
        (long)p.flat_avg,
        (long)p.pileup_pre_pct,
        (long)p.pileup_post_pct, (long)p.shape_on, (long)p.shape_tol,
        (long)p.shape_lmin, (long)p.shape_lmax, (long)p.shape_ln,
        (long)p.shape_ref, (long)p.shape_win, (long)p.shape_hz,
        (unsigned long)mca_emu_baud());
    /* snprintf возвращает длину, которая ПОТРЕБОВАЛАСЬ БЫ. При нехватке
     * места это больше размера буфера, и отправка читала бы за его
     * границей. Ограничиваем после каждого шага. */
    if (n > (int)sizeof(buf) - 1) n = (int)sizeof(buf) - 1;
    /* Список частот: он свой у каждого генератора CLK (см. adc_clk.c),
     * и страница строит выбор по нему, а не по своей копии. */
    for (int i = 0; i < MCA_FREQ_COUNT; i++) {
        n += snprintf(buf + n, sizeof(buf) - n, "%s%lu", i ? "," : "",
                      (unsigned long)mca_freq_table[i]);
        if (n > (int)sizeof(buf) - 1) n = (int)sizeof(buf) - 1;
    }
    n += snprintf(buf + n, sizeof(buf) - n, "]}");
    if (n > (int)sizeof(buf) - 1) n = (int)sizeof(buf) - 1;
    httpd_resp_set_type(r, "application/json");
    return httpd_resp_send(r, buf, n);
}

static esp_err_t h_cmd(httpd_req_t *r)
{
    char q[64], v[16];
    if (httpd_req_get_url_query_str(r, q, sizeof(q)) == ESP_OK &&
        httpd_query_key_value(q, "do", v, sizeof(v)) == ESP_OK) {
        if      (!strcmp(v, "start"))       mca_spec_run  = true;
        else if (!strcmp(v, "stop"))        mca_spec_run  = false;
        else if (!strcmp(v, "scope_start")) mca_scope_run = true;
        else if (!strcmp(v, "scope_stop"))  mca_scope_run = false;
        else if (!strcmp(v, "clear"))       mca_cmd_clear = true;
    }
    return httpd_resp_sendstr(r, "ok");
}

/* /time - часы прибора: {"now": мс Unix-времени (0 - не заданы),
 * "src": 0 - нет, 1 - SNTP, 2 - от браузера}; /time?set=<мс> - время от
 * браузера (принимается, если оно не от SNTP, см. mca_time.h). */
static esp_err_t h_time(httpd_req_t *r)
{
    char q[48], v[24];
    if (httpd_req_get_url_query_str(r, q, sizeof(q)) == ESP_OK &&
        httpd_query_key_value(q, "set", v, sizeof(v)) == ESP_OK)
        mca_time_set_ms(strtoll(v, NULL, 10));
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "{\"now\":%lld,\"src\":%d}",
                     (long long)mca_time_now_ms(), (int)mca_time_source());
    if (n > (int)sizeof(buf) - 1) n = (int)sizeof(buf) - 1;
    httpd_resp_set_type(r, "application/json");
    return httpd_resp_send(r, buf, n);
}

/* /calib - калибровка по энергии. GET отдаёт сохранённую строку JSON (или
 * {}), POST с телом сохраняет её (до MCA_CALIB_MAX байт; пустое тело или {}
 * - стереть). Составляет и разбирает её страница, прибор только хранит.
 * Буферы статические: запросы веб-сервер обрабатывает по одному. */
static char s_calbuf[MCA_CALIB_MAX + 1];

static esp_err_t h_calib_get(httpd_req_t *r)
{
    const size_t n = mca_settings_load_calib(s_calbuf, sizeof(s_calbuf));
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    if (!n) return httpd_resp_sendstr(r, "{}");
    return httpd_resp_send(r, s_calbuf, n);
}

static esp_err_t h_calib_post(httpd_req_t *r)
{
    if (r->content_len > MCA_CALIB_MAX) {
        httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "calibration too long");
        return ESP_FAIL;
    }
    size_t got = 0;
    while (got < r->content_len) {
        const int k = httpd_req_recv(r, s_calbuf + got, r->content_len - got);
        if (k == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (k <= 0) return ESP_FAIL;
        got += (size_t)k;
    }
    s_calbuf[got] = 0;
    size_t len = got;
    if (len == 2 && !strcmp(s_calbuf, "{}")) len = 0;
    if (len && (s_calbuf[0] != '{' || s_calbuf[len - 1] != '}')) {
        httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "not a JSON object");
        return ESP_FAIL;
    }
    const esp_err_t e = mca_settings_save_calib(s_calbuf, len);
    mca_dose_calib_changed();           /* таблицы дозы - по новой шкале */
    httpd_resp_set_type(r, "application/json");
    return httpd_resp_sendstr(r, e == ESP_OK ? "{\"ok\":1}" : "{\"ok\":0}");
}

/* /hist?from=N - история CPS (mca_hist.h), двоичная, little-endian:
 * 7 x uint32 (эпоха, номер первого отданного отсчёта, номер следующего,
 * время прибора с включения в 0.1 с, сколько отдано, длина кольца,
 * резерв), затем отсчёты по 10 байт. Не больше 1024 за раз - страница
 * дочитывает, пока не догонит. /hist?clear=1 - очистить историю. */
static esp_err_t h_hist(httpd_req_t *r)
{
    char q[48], v[16];
    uint32_t from = 0;
    bool fast = false;                  /* f=1 - быстрая история, по 0.1 с */
    if (httpd_req_get_url_query_str(r, q, sizeof(q)) == ESP_OK) {
        if (httpd_query_key_value(q, "clear", v, sizeof(v)) == ESP_OK)
            mca_hist_clear();
        if (httpd_query_key_value(q, "f", v, sizeof(v)) == ESP_OK)
            fast = v[0] == '1';
        if (httpd_query_key_value(q, "from", v, sizeof(v)) == ESP_OK)
            from = (uint32_t)strtoul(v, NULL, 10);
    }
    /* веб-сервер обрабатывает запросы по одному - общий буфер безопасен */
    static mca_hist_sample_t buf[1024];
    uint32_t first = 0, next = 0, epoch = 0;
    const size_t n = mca_hist_copy_ex(fast, from, buf, 1024, &first, &next, &epoch);
    const uint32_t h[7] = { epoch, first, next,
                            (uint32_t)(esp_timer_get_time() / 100000),
                            (uint32_t)n, fast ? MCA_HIST_FAST_LEN : MCA_HIST_LEN,
                            fast ? 100 : 1000 };   /* шаг отсчётов, мс */
    httpd_resp_set_type(r, "application/octet-stream");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    esp_err_t e = httpd_resp_send_chunk(r, (const char *)h, sizeof(h));
    if (e == ESP_OK && n)
        e = httpd_resp_send_chunk(r, (const char *)buf,
                                  n * sizeof(mca_hist_sample_t));
    if (e == ESP_OK) e = httpd_resp_send_chunk(r, NULL, 0);
    return e;
}

/* ---------------- ОСЦИЛЛОГРАФ ПО WEBSOCKET ----------------
 * Страница открывает /ws и присылает туда текстом те же настройки, что
 * и в /scope, плюс run=0|1 (открыта ли вкладка осциллографа и не на паузе
 * ли он). Прибор сам шлёт кадр, как только снимок НОВЫЙ и предыдущий
 * кадр ушёл, не чаще 20 раз в секунду. Раньше страница опрашивала /scope
 * и получала снимок, даже если он не менялся: в ждущем режиме по сети
 * гонялся один и тот же кадр до 66 КБ.
 *
 * Кадр - одно двоичное сообщение: 10 x int32 (tg, rise, age, lvl, длина
 * окна, высота импульса, отброшено фильтром, номер снимка, число
 * отсчётов, способ записи: 0 - uint16, 1 - сжатие scope_codec), затем
 * отсчёты. Сжатие без потерь уменьшает кадр в 3.5-4 раза. Сообщение
 * режется на куски по 4 КБ (фрагменты WebSocket) - буфер во внутренней
 * памяти небольшой, а сжатие идёт на лету.
 *
 * Отправка - в задаче веб-сервера (httpd_queue_work): так она не
 * пересекается с его собственными служебными кадрами на том же сокете.
 * Если WebSocket не открылся, страница опрашивает /scope, как раньше. */
#define WS_HDR      40          /* 10 x int32                          */
#define WS_MIN_US   50000       /* не чаще 20 кадров в секунду          */
#define WS_ENC_RAW  0
#define WS_ENC_NIB  1

static httpd_handle_t    s_srv;
static volatile int      s_ws_fd = -1;      /* клиент осциллографа        */
static volatile bool     s_ws_run;          /* ему нужны кадры            */
static volatile bool     s_ws_busy;         /* кадр в очереди или уходит  */
static volatile bool     s_ws_force;        /* отдать снимок, даже старый */
static volatile size_t   s_ws_want = DIAG_SCOPE_LEN;
static volatile size_t   s_ws_pre  = DIAG_SCOPE_PRE;
static uint32_t          s_ws_seq;          /* номер отправленного снимка */
static bool              s_codec_ok;
static uint8_t           s_ws_out[4096];    /* кусок сообщения            */

static void ws_send_scope(void *arg)
{
    const int fd = s_ws_fd;
    if (fd < 0 ||
        httpd_ws_get_fd_info(s_srv, fd) != HTTPD_WS_CLIENT_WEBSOCKET) {
        if (fd == s_ws_fd) s_ws_fd = -1;
        s_ws_busy = false;
        return;
    }
    mca_prof_web_begin(PROF_EP_SCOPE);

    int32_t tg = -1, rise = 0, age = -1, amp = -1;
    uint32_t rej = 0, seq = 0;
    size_t full = 0;
    const uint16_t *d = NULL;
    const size_t n = mca_diag_scope_acquire(s_ws_want, s_ws_pre, &d, &tg,
                                            &rise, &age, &full, &amp, &rej,
                                            &seq);
    const bool force = s_ws_force;
    s_ws_force = false;

    esp_err_t e = ESP_OK;
    if (seq != s_ws_seq || force) {
        const int32_t enc = s_codec_ok ? WS_ENC_NIB : WS_ENC_RAW;
        const int32_t h[10] = { tg, rise, age, mca_diag_get_trig_level(),
                                (int32_t)full, amp, (int32_t)rej,
                                (int32_t)seq, (int32_t)n, enc };
        memcpy(s_ws_out, h, WS_HDR);

        scodec_t cs = { 0 };
        size_t off = WS_HDR, pos = 0;
        bool first = true;
        for (;;) {
            size_t used = 0;
            if (n && enc == WS_ENC_NIB) {
                pos += scodec_encode(&cs, d + pos, n - pos, s_ws_out + off,
                                     sizeof(s_ws_out) - off, &used);
                if (pos >= n) used += scodec_finish(&cs, s_ws_out + off + used);
            } else if (n) {
                size_t k = (sizeof(s_ws_out) - off) / sizeof(uint16_t);
                if (k > n - pos) k = n - pos;
                memcpy(s_ws_out + off, d + pos, k * sizeof(uint16_t));
                pos  += k;
                used  = k * sizeof(uint16_t);
            }
            const bool last = pos >= n;
            httpd_ws_frame_t fr = {
                .final      = last,
                .fragmented = true,
                .type       = first ? HTTPD_WS_TYPE_BINARY
                                    : HTTPD_WS_TYPE_CONTINUE,
                .payload    = s_ws_out,
                .len        = off + used,
            };
            e = httpd_ws_send_frame_async(s_srv, fd, &fr);
            first = false;
            off   = 0;
            if (e != ESP_OK || last) break;
        }
        if (e == ESP_OK) s_ws_seq = seq;
    }
    mca_diag_scope_release();

    if (e != ESP_OK && fd == s_ws_fd) s_ws_fd = -1;   /* клиент ушёл */
    mca_prof_web_end(PROF_EP_SCOPE);
    s_ws_busy = false;
}

/* Раз в 10 мс: есть ли кому и что отправить. Сам кадр собирает и шлёт
 * задача веб-сервера. */
static void ws_task(void *arg)
{
    int64_t last = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10));
        if (s_ws_fd < 0 || !s_ws_run || s_ws_busy) continue;
        if (!s_ws_force && mca_diag_scope_seq() == s_ws_seq) continue;
        const int64_t now = esp_timer_get_time();
        if (now - last < WS_MIN_US) continue;
        last = now;
        s_ws_busy = true;
        if (httpd_queue_work(s_srv, ws_send_scope, NULL) != ESP_OK)
            s_ws_busy = false;
    }
}

static esp_err_t h_ws(httpd_req_t *r)
{
    if (r->method == HTTP_GET) {            /* рукопожатие */
        /* новое подключение не перебивает клиента, которому идут кадры:
         * поток он заберёт, когда сам попросит кадры (run=1) */
        if (s_ws_fd < 0 || !s_ws_run) {
            s_ws_fd  = httpd_req_to_sockfd(r);
            s_ws_run = false;               /* до первых настроек */
        }
        return ESP_OK;
    }
    httpd_ws_frame_t f = { .type = HTTPD_WS_TYPE_TEXT };
    esp_err_t e = httpd_ws_recv_frame(r, &f, 0);
    if (e != ESP_OK) return e;
    char q[160];
    if (f.len >= sizeof(q)) return ESP_FAIL;    /* не наше - закрыть */
    f.payload = (uint8_t *)q;
    e = httpd_ws_recv_frame(r, &f, sizeof(q) - 1);
    if (e != ESP_OK) return e;
    q[f.len] = 0;
    if (f.type != HTTPD_WS_TYPE_TEXT) return ESP_OK;

    /* КАДРЫ ПОЛУЧАЕТ ПОСЛЕДНИЙ КЛИЕНТ, КОТОРОМУ ОНИ НУЖНЫ (run=1).
     * Страница шлёт настройки раз в 2 с и тогда, когда осциллограф у неё
     * на паузе, в фоне или открыт другой режим, - с run=0. Раньше такая
     * посылка забирала поток себе и останавливала его: у вкладки, где
     * смотрят осциллограф, картинка замирала до её следующей посылки, до
     * 2 с (так и было: в браузере осталась вторая вкладка на паузе).
     * Теперь run=0 от другого клиента не трогает ни поток, ни окно снимка. */
    const int fd = httpd_req_to_sockfd(r);
    int32_t run = 0;
    const bool has_run = q_int(q, "run", &run);
    if (has_run && !run && fd != s_ws_fd && s_ws_fd >= 0 && s_ws_run)
        return ESP_OK;

    size_t want = DIAG_SCOPE_LEN, pre = DIAG_SCOPE_PRE;
    scope_apply(q, &want, &pre);
    s_ws_want = want;
    s_ws_pre  = pre;
    mca_diag_set_scope_want(want, pre);
    if (has_run) s_ws_run = run != 0;
    /* новые настройки - сразу кадр, не дожидаясь следующего снимка */
    s_ws_fd    = fd;
    s_ws_force = true;
    return ESP_OK;
}

/* Сеть целиком: NVS, стек TCP/IP, Ethernet.
 *
 * WIFI В ЭТОЙ КОПИИ НЕТ. Плата - Waveshare ESP32-S3-ETH с питанием PoE:
 * прибор работает только по Ethernet, радио не включается вовсе (заодно
 * нечему ловить гармоники CLK АЦП, см. README). Прежде здесь были точка
 * доступа MCA-Setup, клиент WiFi и сторож, включавший WiFi, если у
 * Ethernet 30 с нет адреса. Теперь без адреса по DHCP прибор по сети
 * недоступен - адрес и ошибки видны в консоли (USB). */
static void net_init(void)
{
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    esp_netif_init();
    esp_event_loop_create_default();

    if (mca_eth_start() != ESP_OK)
        ESP_LOGE(TAG, "W5500 не отвечает - сети нет, прибор по сети недоступен "
                      "(WiFi в этой прошивке нет)");
    /* часы прибора: SNTP, когда появится выход в интернет */
    mca_time_init();
}

/* ---- шапка и навигация вспомогательных страниц ---- */
/* Та же шапка, что на главной, но без индикаторов состояния - только
 * переключатель языка. Заголовок вкладки - на двух языках. */
#define SUB_HEAD(TITLE, TITLE_EN) \
"<!DOCTYPE html><html lang=ru><head><meta charset=utf-8><link rel=icon href=/favicon.ico>" \
"<meta name=viewport content='width=device-width,initial-scale=1'>" \
"<title data-en='" TITLE_EN " · MCA'>" TITLE " · MCA</title>" \
"<link rel=stylesheet href=/s.css><script src=/l.js></script></head><body>" \
"<header><div class=brand><img src=/logo.png?v=2 alt=''>MCA " \
"<span style=color:var(--accent)>AD9226</span></div>" \
"<div class=chips><span class='seg lng'><button onclick=\"setLang('ru')\">RU</button>" \
"<button onclick=\"setLang('en')\">EN</button></span></div></header>"
#define SUB_NAV(S, D, H) "<nav><a href=/#spectrum data-en='Spectrum'>Спектр</a>" \
    "<a href=/#scope data-en='Oscilloscope'>Осциллограф</a>" S D H "</nav>"
#define N_SET    "<a href=/settings data-en='Settings'>Настройки</a>"
#define N_SET_ON "<span class=on data-en='Settings'>Настройки</span>"
#define N_DIAG    "<a href=/diag data-en='Diagnostics'>Диагностика</a>"
#define N_DIAG_ON "<span class=on data-en='Diagnostics'>Диагностика</span>"
#define N_HELP    "<a href=/help data-en='Help'>Справка</a>"
#define N_HELP_ON "<span class=on data-en='Help'>Справка</span>"

/* ---- страница «Настройки»: сеть, прошивка, настройки АЦП, диагностика АЦП ----
 * Таблица параметров и диагностика тракта жили на вкладках «Спектр» и
 * «Осциллограф» под галочками, сеть и прошивка - на странице «Сеть». Всё
 * собрано здесь; прежние адреса /net и /wifi открывают эту страницу. */
static const char SET_PAGE[] =
SUB_HEAD("Настройки", "Settings") SUB_NAV(N_SET_ON, N_DIAG, N_HELP)
"<div class=wrap>"
"<div style='display:flex;gap:14px;flex-wrap:wrap;align-items:stretch'>"
"<section class='panel pad' style='flex:1 1 360px'>"
"<h3 data-en='Network'>Сеть</h3>"
"<div class=grp style='margin:10px 0'>"
"<label><input type=radio name=nm id=nmd onchange=nmode()> <span data-en='address from the router (DHCP)'>адрес от роутера (DHCP)</span></label>"
"<label><input type=radio name=nm id=nms onchange=nmode()> <span data-en='static address'>статический адрес</span></label></div>"
"<table class=ptab id=nst>"
"<tr><td data-en='Address'>Адрес</td><td><input id=n_ip placeholder=192.168.1.50 style=width:220px></td></tr>"
"<tr><td data-en='Mask'>Маска</td><td><input id=n_mask placeholder=255.255.255.0 style=width:220px></td></tr>"
"<tr><td data-en='Gateway'>Шлюз</td><td><input id=n_gw placeholder=192.168.1.1 style=width:220px></td></tr>"
"<tr><td>DNS</td><td><input id=n_dns placeholder=192.168.1.1 style=width:220px></td></tr></table>"
"<button class='btn green' onclick=netSave() style=margin-top:8px data-en='Save address'>Сохранить адрес</button>"
"<div id=nmsg class=n style=margin-top:8px></div>"
"<p class=n data-en='A new static address is applied at once, and this page opens on it. If it does not "
"open from the new address within 60 s (wrong subnet or gateway), the device returns to the previous "
"settings. Holding the BOOT button for 5 s while the device is running switches back to DHCP.'>"
"Новый статический адрес применяется сразу, и эта страница открывается по нему. Если за 60 с она "
"не откроется с нового адреса (не та подсеть или шлюз), прибор вернётся к прежним настройкам. Кнопка "
"BOOT, удержанная 5 с во время работы, возвращает DHCP.</p>"
"</section>"
/* обновление прошивки по сети: файл build/esp32_mca.bin, см. mca_ota.c */
"<section class='panel pad' style='flex:1 1 360px'>"
"<h3 data-en='Firmware'>Прошивка</h3>"
"<div id=fw class=n style=margin-bottom:10px>...</div>"
"<input id=fwf type=file accept=.bin style='width:100%;margin:4px 0 10px'>"
"<button class='btn amber' onclick=fwup() data-en='Upload and restart'>Загрузить и перезапустить</button>"
"<div id=fwp class=n style=margin-top:8px></div>"
"<p class=n data-en='File build/esp32_mca.bin from the project build. If the new firmware does not "
"come up on the network, the next restart brings back the previous one.'>Файл build/esp32_mca.bin "
"из сборки проекта. Если новая прошивка не выйдет на связь, следующая перезагрузка вернёт прежнюю.</p>"
"</section></div>"
"<section class='panel pad'>"
"<h3 data-en='ADC settings'>Настройки АЦП</h3>"
"<div style=overflow-x:auto><table class=ptab id=pbox></table></div>"
"<div class=grp style=margin-top:10px><button class='btn green' onclick=apply() data-en='Apply'>Применить</button>"
"<span class=mut style=font-size:11.5px data-en='values go to the device only with this button and are saved in its memory'>"
"в прибор значения уходят только по этой кнопке и "
"сохраняются в его памяти</span></div>"
"</section>"
/* фильтр формы (mca_dsp.c, shape_check): включается здесь же, сразу */
"<section class='panel pad'>"
"<h3 data-en='Pulse shape filter'>Фильтр формы импульса</h3>"
"<p class=n style=margin-top:0 data-en='Rejects pulses that differ from the learned reference. Helps with a noisy power supply or when working with a poor-quality PMT.'>"
"Отбрасывает импульсы, отличающиеся от набранного эталона. Помогает при шумном питании или при работе с некачественным ФЭУ.</p>"
"<div class=grp style='margin:8px 0'>"
"<label><input type=checkbox id=sh_on onchange=shSet()> <span data-en='filter on'>фильтр включён</span></label>"
"<label><span data-en='tolerance, %'>допуск, %</span> <input id=sh_tol type=number min=1 max=200 style=width:70px onchange=shSet()></label>"
"<label title='какие импульсы брать в эталон, как запись эталона в BecqMoni: высота от / до в кодах (0 - выше трёх порогов / без верхней границы; зашкалившие не берутся никогда) и сколько импульсов' "
"data-en-title='which pulses go into the reference, like reference recording in BecqMoni: height from / to in codes (0 - above three thresholds / no upper limit; clipped pulses are never taken) and how many pulses'>"
"<span data-en='reference: height from'>в эталон: высота от</span> <input id=sh_lmin type=number min=0 max=4095 style=width:70px onchange=shSet()> "
"<span data-en='to'>до</span> <input id=sh_lmax type=number min=0 max=4095 style=width:70px onchange=shSet()> "
"<span data-en='codes, pulses'>кодов, импульсов</span> <input id=sh_ln type=number min=100 max=2000 step=100 style=width:80px onchange=shSet()></label>"
"<button class='btn green sm' onclick=shLearn() id=sh_lb></button>"
"<button class='btn sm' onclick=shClr() data-en='Clear histogram'>Очистить гистограмму</button></div>"
"<div id=sh_st class=n style=line-height:1.6></div>"
"<div style='display:flex;gap:14px;flex-wrap:wrap;margin-top:8px'>"
"<div style='flex:1 1 380px'><div class=n style=margin-bottom:4px><b data-en='Reference pulse'>Эталонный импульс</b></div>"
"<canvas id=sh_wv style='width:100%;height:230px;display:block'></canvas>"
"<div id=sh_wl class=mut style=font-size:11.5px;margin-top:4px;line-height:1.6></div></div>"
"<div style='flex:1 1 380px'><div class=n style=margin-bottom:4px><b data-en='Pulse width'>Ширина импульсов</b></div>"
"<canvas id=sh_hg style='width:100%;height:230px;display:block'></canvas>"
"<div id=sh_hl class=mut style=font-size:11.5px;margin-top:4px;line-height:1.6></div></div></div>"
"</section>"
"</div>"
"<script>i18n();"
/* Список частот присылает прибор (/cfg, поле fl): он зависит от
   генератора CLK в прошивке. Выбор уходит номером в этом списке.
   Пометка у 20 МГц: по замеру на этой плате (2026-10-28, ~3500 имп/с)
   обработка спектра занимает ~90 % ядра, потерь не было; осциллограф с
   длинной развёрткой на 20 МГц чанки теряет. */
"var FHZ=[];"
"function fqName(h){return +(h/1e6).toFixed(2)+L(' МГц',' MHz')+"
"(h>=19e6?L(' (предел)',' (limit)'):'')}"
/* ТАБЛИЦА НАСТРОЕК. Третий столбец - на что параметр влияет:
   b - при любом способе, t - только трапеция, i - только интегрирование.
   Сверено с mca_dsp.c: событие в обоих способах ищется по трапеции
   (порог, L, G, гистерезис, перезапуск, поиск пика, наложения; по L и G
   при интегрировании ещё и находится вершина), а амплитуда считается
   по-разному. Параметры другого способа не прячем, а приглушаем:
   видно, что они есть и что сейчас не действуют. */
/* Наверху - то, что меняют обычно: частота, порог, полярность и шкала.
   Остальное - в раскрывающемся блоке «Дополнительно» (группы с 1 в
   третьем элементе): окна, способ интегрирования и отбраковка. */
"var GR=["
"[L('Основное','Main'),["
"['fq',L('Частота','Sample rate'),'b',L('Частота АЦП. Выше - подробнее форма импульса, но больше нагрузка','ADC sample rate. Higher - more detailed pulse shape, but more load')],"
"['threshold',L('Порог','Threshold'),'b',L('Порог по выходу трапеции, коды АЦП. Ставить выше шума: см. «шум фильтра» под графиком','Threshold on the trapezoid output, ADC codes. Set above the noise: see “filter noise” under the plot')],"
"['polarity',L('Полярность','Polarity'),'b',L('0 - импульсы вверх, 1 - вниз','0 - pulses up, 1 - down')]"
"]],"
"[L('Шкала спектра','Spectrum scale'),["
"['cpc',L('Кодов на канал','Codes per channel'),'b',L('Канал = амплитуда / это число. Смена очищает спектр.','Channel = amplitude / this number. Changing it clears the spectrum.')+' <b id=cpctop></b>'],"
"['nch',L('Каналов','Channels'),'b',L('Длина шкалы вправо. Спектр не сбрасывает','Scale length to the right. Does not reset the spectrum')]"
"]],"
"[L('Обнаружение импульса','Pulse detection'),["
"['trap_L',L('Окно L','Window L'),'b',L('Окно трапеции, отсч. При трапеции задаёт ещё и амплитуду',"
"'Trapezoid window, samples. With the trapezoid method it also sets the amplitude')],"
"['trap_G',L('Зазор G','Gap G'),'b',L('Отступ вычитаемого окна, отсч. G−L - длина плоской вершины',"
"'Offset of the subtracted window, samples. G−L is the flat-top length')],"
"['hysteresis',L('Гистерезис %','Hysteresis %'),'b',L('Ниже какой доли порога должна упасть трапеция, чтобы ловить следующий','Fraction of the threshold the trapezoid must drop below before the next pulse is caught')],"
"['rearm',L('Перезапуск','Re-arm'),'b',L('Отсч. после пика, когда новые импульсы не ловятся',"
"'Samples after the peak during which new pulses are not caught')],"
"['search',L('Поиск пика','Peak search'),'b',L('Сколько отсч. после порога искать вершину',"
"'How many samples after the threshold to search for the peak')]"
"],1],"
"[L('Амплитуда','Amplitude'),["
"['algo',L('Способ измерения','Method'),'',L('Трапеция или интегрирование. Смена очищает спектр','Trapezoid or integration. Changing it clears the spectrum')],"
"['flat_avg',L('Средн. по плато','Flat-top avg'),'t',L('1 - среднее по плоской вершине вместо максимума. Работает только при G−L ≥ 16','1 - mean over the flat top instead of the maximum. Works only when G−L ≥ 16')],"
"['int_rise',L('До вершины','Before peak'),'i',L('Отсч. до вершины в среднем. Линейка: B−A. Это же окно - у фильтра формы','Samples before the peak, on average. Ruler: B−A. Also the shape filter window')],"
"['int_fall',L('После вершины','After peak'),'i',L('Отсч. после вершины. Линейка: C−B. Длиннее окно - пик левее. Это же окно - у фильтра формы','Samples after the peak. Ruler: C−B. Longer window - the peak moves left. Also the shape filter window')],"
"['baseline_shift',L('База 2^N','Baseline 2^N'),'i',L('Скорость слежения за базовой линией (трапеции база не нужна)','Baseline tracking speed (the trapezoid does not need a baseline)')],"
"['baseline_win',L('Окно базы','Baseline window'),'i',L('Отсчёты дальше этого от базы в неё не берутся, коды','Samples farther than this from the baseline are not used for it, codes')]"
"],1],"
"[L('Отбраковка наложений','Pile-up rejection'),["
"['pileup_pre_pct',L('Наложение до %','Pile-up before %'),'b',L('Брак, если перед пиком трапеция выше этого % амплитуды. 0 - выключено','Reject if before the peak the trapezoid is above this % of the amplitude. 0 - off')],"
"['pileup_post_pct',L('Наложение после %','Pile-up after %'),'b',L('Брак, если после пика не упала ниже этого %. Работают, только если оба больше 0','Reject if after the peak it has not dropped below this %. Active only if both are above 0')]"
"],1],"
"[L('Связь с программами на ПК','PC software link'),["
"['emu',L('Эмуляция MCA на COM','MCA emulation on COM'),'b',L('Порт UART0 отвечает по протоколу shproto (BecqMoni и др.). Консоль при этом молчит','UART0 answers using the shproto protocol (BecqMoni etc.). The console goes silent')]"
"],1]];"
/* fq и emu - не параметры обработки: уходят в прибор сразу, своими запросами */
"var P=[];GR.forEach(function(g){g[1].forEach(function(r){if(r[0]!='fq'&&r[0]!='emu')P.push(r[0])})});"
/* пометка только у параметров одного способа; без пометки - действует всегда */
"var BDG={t:[L('трапеция','trapezoid'),'t'],i:[L('интегрирование','integration'),'i']};"
/* поля выпадающими меню: значение и подпись */
"var SEL={algo:[['0',L('трапеция','trapezoid')],['1',L('интегрирование','integration')],['2',L('свёртка с эталоном','convolution with the reference')]],"
"nch:[['2048','2048'],['4096','4096'],['8192','8192']],"
"emu:[['0',L('выключена','off')],['38400','38400'],['115200','115200'],['460800','460800'],"
"['600000','600000'],['921600','921600']]};"
"var H={"
"emu:L('Эмуляция анализатора на последовательном порту UART0 - разъём UART/COM на плате (не встроенный USB). Прибор отвечает по бинарному протоколу shproto, и программы на ПК, умеющие работать с такими анализаторами (например, BecqMoni), видят его как MCA: набор, остановка, сброс, выгрузка спектра и статуса, калибровка. Скорость порта выбирается здесь, действует сразу и сохраняется. Пока эмуляция включена, консоль молчит: сообщения прошивки на порт не идут. Программе отдаётся столько каналов, сколько выбрано в «Каналов»: в BecqMoni выставьте то же число каналов, тогда они совпадут с прибором один в один. Заводские команды настройки из программы не принимаются - параметры задаются на этой странице.',"
"'Analyzer emulation on the serial port UART0 - the UART/COM connector on the board (not the native USB). The device answers using the binary shproto protocol, and PC programs that work with such analyzers (for example, BecqMoni) see it as an MCA: start, stop, reset, spectrum and status upload, calibration. The port speed is selected here, takes effect immediately and is saved. While emulation is on, the console is silent: firmware messages are not sent to the port. The program receives as many channels as selected in “Channels”: set the same number of channels in BecqMoni, then they match the device one to one. Factory tuning commands from the program are not accepted - parameters are set on this page.'),"
"fq:L('Частота семплирования АЦП. Выше - подробнее форма импульса, но обработка может не успевать: смотрите потерянные чанки на странице «Диагностика». По замеру на этой плате (около 3500 имп/с) обработка спектра занимает около 70 % ядра на 16 МГц и около 90 % на 20 МГц; при большем счёте нагрузка выше. Параметры фильтра заданы в отсчётах, поэтому на другой частоте то же L или G - другое время.',"
"'ADC sample rate. Higher - more detailed pulse shape, but processing may not keep up: watch lost chunks on the “Diagnostics” page. Measured on this board (about 3500 cps), spectrum processing takes about 70 % of the core at 16 MHz and about 90 % at 20 MHz; with a higher count rate the load is higher. Filter parameters are set in samples, so at another rate the same L or G is a different time.'),"
"polarity:L('0 - импульсы вверх от базовой линии, 1 - вниз (например, анод ФЭУ напрямую). При 1 сигнал переворачивается ещё до обработки, и порог и спектр работают как с импульсами вверх; осциллограф синхронизируется по фронту вниз. Постоянная составляющая сигнала на обработку не влияет - её вычитает трапеция.',"
"'0 - pulses go up from the baseline, 1 - down (for example, PMT anode directly). With 1 the signal is inverted before processing, so the threshold and spectrum work as with upward pulses; the oscilloscope triggers on a falling edge. A DC offset of the signal does not affect processing - the trapezoid subtracts it.'),"
"algo:L('Трапеция: амплитуда по вершине трапеции (окно L, зазор G, по желанию среднее по плато). Интегрирование: среднее отсчётов импульса вокруг вершины (до и после вершины) за вычетом базы - 32 отсчётов перед фронтом этого импульса. Свёртка с эталоном: высота, при которой эталонный импульс фильтра формы лучше всего ложится на импульс - в неё идёт весь импульс, каждый отсчёт с весом своей доли сигнала (так берёт высоту BecqMoni для звуковой карты), база - 32 отсчёта перед началом фронта, шкала - как у интегрирования; нужен набранный эталон на этом окне «до / после вершины» и этой частоте, без него амплитуда считается трапецией; новый эталон очищает спектр. Поля, которые при выбранном способе ни на что не влияют, в таблице приглушены. Обнаружение события в обоих способах одинаковое - по выходу трапеции, поэтому L, G, порог, гистерезис, перезапуск и поиск пика нужны всегда. Шкалы трапеции и интегрирования различаются (свёртка - в шкале интегрирования) - после переключения подберите «Кодов на канал». Смена способа очищает спектр.',"
"'Trapezoid: amplitude from the trapezoid top (window L, gap G, optionally flat-top average). Integration: mean of the pulse samples around the peak (before and after peak) minus the baseline - 32 samples before the rise of this pulse. Convolution with the reference: the height at which the shape-filter reference pulse fits the pulse best - the whole pulse goes into it, each sample weighted by its share of the signal (this is how BecqMoni takes the height for a sound card), the baseline is 32 samples before the rise starts, the scale is that of integration; it needs a learned reference for this “before / after peak” window and this rate, without one the amplitude is computed by the trapezoid; a new reference clears the spectrum. Fields that have no effect with the selected method are dimmed in the table. Event detection is the same for both methods - on the trapezoid output, so L, G, threshold, hysteresis, re-arm and peak search are always needed. The trapezoid and integration scales differ (convolution uses the integration scale) - after switching, adjust “Codes per channel”. Changing the method clears the spectrum.'),"
"int_rise:L('Интегрирование: сколько отсчётов ДО вершины включать в сумму. Смотрите на картинку импульса и считайте по сетке.',"
"'Integration: how many samples BEFORE the peak to include in the sum. Look at the pulse picture and count by the grid.'),"
"int_fall:L('Интегрирование: сколько отсчётов ПОСЛЕ вершины включать в сумму. Обычно заметно больше, чем до вершины, потому что спад длиннее фронта.',"
"'Integration: how many samples AFTER the peak to include in the sum. Usually noticeably more than before the peak, because the decay is longer than the rise.'),"
"cpc:L('Сколько кодов амплитуды приходится на один канал спектра: 1 - канал равен коду АЦП, 0.5 - вдвое подробнее, 2 - вдвое грубее. Верх шкалы = каналов x кодов на канал, он подписан рядом. При базе в середине АЦП запас вверх около 2047 кодов, поэтому 2048 каналов по 1 коду как раз покрывают всю шкалу. Амплитуда в обоих способах в кодах: трапеция - высота импульса, интегрирование - средняя высота в окне. Изменение этого числа очищает спектр: старые события разложены по другой шкале.',"
"'How many amplitude codes per spectrum channel: 1 - a channel equals an ADC code, 0.5 - twice as fine, 2 - twice as coarse. Top of scale = channels x codes per channel, shown next to it. With the baseline in the middle of the ADC there are about 2047 codes of headroom, so 2048 channels of 1 code cover the whole scale. The amplitude is in codes for both methods: trapezoid - pulse height, integration - mean height in the window. Changing this number clears the spectrum: old events are laid out on a different scale.'),"
"nch:L('Сколько каналов показывать: 2048, 4096 или 8192. Это длина шкалы: верх = каналов x кодов на канал. Раскладка событий по каналам от этого не зависит - спектр не сжимается и не сбрасывается, меняется только, докуда видно вправо. Прибор копит все 8192 канала, так что переключать можно в любой момент.',"
"'How many channels to show: 2048, 4096 or 8192. This is the scale length: top = channels x codes per channel. How events are laid out in channels does not depend on it - the spectrum is not compressed or reset, only how far to the right is visible changes. The device accumulates all 8192 channels, so you can switch at any time.'),"
"threshold:L('Порог по выходу трапеции, В КОДАХ АЦП (нормирован на длину окна, поэтому не зависит от L). Ставится выше шума трапеции - см. строку «шум фильтра» на вкладке «Осциллограф».',"
"'Threshold on the trapezoid output, IN ADC CODES (normalized to the window length, so it does not depend on L). Set above the trapezoid noise - see the “filter noise” line on the “Oscilloscope” tab.'),"
"hysteresis:L('Доля порога В ПРОЦЕНТАХ, ниже которой должна опуститься трапеция, чтобы детектор снова взвёлся. 50 означает половину порога. Прежний вариант вычитал единицу, что означало почти полное отсутствие гистерезиса.',"
"'Fraction of the threshold IN PERCENT that the trapezoid must drop below before the detector re-arms. 50 means half the threshold. The previous version subtracted one, which meant almost no hysteresis.'),"
"trap_L:L('Длина окна интегрирования: сколько отсчётов импульса суммируется. Именно это окно определяет, какая часть импульса пойдёт в амплитуду, и насколько усреднится шум. ВНИМАНИЕ: в приборах, где окно задаётся парой RISE и FALL, окно интегрирования - это их сумма: RISE=6 FALL=15 соответствует нашему L=21. При интегрировании L и G тоже работают: по трапеции находится импульс и место его вершины.',"
"'Integration window length: how many pulse samples are summed. This window determines which part of the pulse goes into the amplitude and how much the noise is averaged. NOTE: in devices where the window is set by a RISE and FALL pair, the integration window is their sum: RISE=6 FALL=15 corresponds to our L=21. With integration L and G also work: the trapezoid finds the pulse and its peak position.'),"
"trap_G:L('На сколько отсчётов назад отстоит второе окно, которое вычитается. Оно должно целиком лежать на базовой линии ДО импульса, поэтому G обязан быть больше L плюс длительность фронта. Разность G минус L даёт длину плоской вершины.',"
"'How many samples back the second, subtracted window is. It must lie entirely on the baseline BEFORE the pulse, so G must be greater than L plus the rise time. G minus L gives the flat-top length.'),"
"rearm:L('Сколько отсчётов жёстко пропустить после пика, независимо от гистерезиса. Защита от дребезга на спадающем хвосте. Это НЕ метрика потерь, а параметр перезапуска детектора.',"
"'How many samples to skip unconditionally after the peak, regardless of hysteresis. Protection against chatter on the falling tail. This is NOT a loss metric but a detector re-arm parameter.'),"
"search:L('Сколько отсчётов после срабатывания порога перебирать в поисках максимума трапеции. Должно накрывать весь импульс.',"
"'How many samples after the threshold crossing to search for the trapezoid maximum. Must cover the whole pulse.'),"
"baseline_shift:L('Бегущая база - запасная для интегрирования (основная - 32 отсчёта перед фронтом импульса); трапеция вычитает базу сама. Постоянная времени фильтра, отслеживающего нулевой уровень. Больше значение - медленнее и плавнее подстройка. Обычно 8-12, допустимо 1-16.',"
"'Running baseline - a fallback for integration (the main one is 32 samples before the rise of the pulse); the trapezoid subtracts the baseline itself. Time constant of the filter that tracks the zero level. Larger value - slower and smoother tracking. Usually 8-12, allowed 1-16.'),"
"flat_avg:L('Только для трапеции. 1 = амплитуда как среднее по плоской вершине трапеции, 0 = по максимуму. Проверено симуляцией: усреднение выигрывает ТОЛЬКО при длинном плато (G минус L не меньше 19). При коротком плато максимум даёт даже меньший разброс, поэтому при (G-L) меньше 16 происходит автоматический откат на максимум. Смещение максимума растёт с шумом, но это постоянный сдвиг для всех амплитуд - уходит в калибровку, ширину пиков не портит.',"
"'Trapezoid only. 1 = amplitude as the mean over the flat top of the trapezoid, 0 = maximum. Verified by simulation: averaging wins ONLY with a long flat top (G minus L at least 19). With a short flat top the maximum gives even less spread, so with (G-L) below 16 it automatically falls back to the maximum. The bias of the maximum grows with noise, but it is a constant shift for all amplitudes - it goes into calibration and does not widen the peaks.'),"
"baseline_win:L('Только для бегущей базы (запасной у интегрирования). Окно приёма отсчёта в базовую линию, коды АЦП. В среднее берутся только отсчёты, лежащие ближе этого значения к текущей оценке нуля - иначе длинные хвосты импульсов утянут базовую линию вверх. Ставить примерно вдвое-втрое больше размаха шума.',"
"'Running baseline only (a fallback for integration). Acceptance window for a sample into the baseline, ADC codes. Only samples closer than this to the current zero estimate are averaged - otherwise long pulse tails would pull the baseline up. Set about two to three times the noise swing.'),"
"pileup_pre_pct:L('Если ПЕРЕД пиком трапеция уже выше этого процента от амплитуды - импульс сидит на хвосте предыдущего, бракуем. Меньше процент - строже отбраковка.',"
"'If BEFORE the peak the trapezoid is already above this percentage of the amplitude - the pulse sits on the tail of the previous one, reject. Lower percentage - stricter rejection.'),"
"pileup_post_pct:L('Если ПОСЛЕ пика трапеция не упала ниже этого процента - на импульс наложился следующий, бракуем. Меньше процент - строже отбраковка.',"
"'If AFTER the peak the trapezoid has not dropped below this percentage - the next pulse piled up on this one, reject. Lower percentage - stricter rejection.'),"
"};"
"var fs=document.createElement('select');fs.id='fq';fs.onchange=setfreq;"
/* Пересборка списка сбрасывала выбор на первый пункт: после «Применить»
   на экране стояло 1 МГц, хотя прибор работал на другой частоте. Ставим
   пункт, ближайший к частоте прибора (REALHZ из /stat). */
"function fillFq(){fs.innerHTML='';var bi=0,bd=1e18;FHZ.forEach(function(h,i){"
"var o=document.createElement('option');o.value=i;o.text=fqName(h);fs.add(o);"
"var d=Math.abs(h-(window.REALHZ||0));if(d<bd){bd=d;bi=i}});if(FHZ.length)fs.value=bi}"
/* строки таблицы: подпись (подробная подсказка - при наведении), поле,
   на какой способ влияет, коротко что делает */
"var pt='',adv0=0;"
"GR.forEach(function(g){var a=g[2]?' data-adv=1':'';"
"if(g[2]&&!adv0){adv0=1;pt+='<tr class=gh><td colspan=4><a href=# id=advl onclick=\"advTog();return false\"></a></td></tr>'}"
"pt+='<tr class=gh'+a+'><td colspan=4>'+g[0]+'</td></tr>';"
"g[1].forEach(function(r){var k=r[0],b=BDG[r[2]],c;"
"if(k=='fq')c='<span id=fqslot></span>';"
"else if(SEL[k])c='<select id=p_'+k+'>'+SEL[k].map(function(o){"
"return '<option value='+o[0]+'>'+o[1]+'</option>'}).join('')+'</select>';"
"else c='<input id=p_'+k+' type=number'+(k=='cpc'?' step=0.05':'')+'>';"
"pt+='<tr id=w_'+k+a+'><td><label title=\"'+(H[k]||'').replace(/\"/g,'&quot;')+'\">'+r[1]+'</label></td>'+"
"'<td>'+c+'</td><td>'+(b?'<span class=\"bdg '+b[1]+'\">'+b[0]+'</span>':'')+'</td>'+"
"'<td>'+r[3]+'</td></tr>'})});"
"var pb=document.getElementById('pbox');pb.innerHTML=pt;"
/* «Дополнительно» свёрнуто по умолчанию; состояние помнит браузер */
"var ADV=0;try{ADV=localStorage.getItem('adv')=='1'}catch(e){}"
"function advShow(){var rs=document.querySelectorAll('#pbox tr[data-adv]');"
"for(var i=0;i<rs.length;i++)rs[i].style.display=ADV?'':'none';"
"document.getElementById('advl').textContent=(ADV?'▾ ':'▸ ')+L('Дополнительно','Advanced')}"
"function advTog(){ADV=!ADV;try{localStorage.setItem('adv',ADV?'1':'0')}catch(e){}advShow()}"
"advShow();"
"document.getElementById('fqslot').appendChild(fs);"
/* способ измерения: строки другого способа приглушаются */
/* окно «до / после вершины» нужно и фильтру формы - при нём не приглушаем */
"function showFields(){var al=+document.getElementById('p_algo').value,so=document.getElementById('sh_on').checked||al==2;"
"GR.forEach(function(g){g[1].forEach(function(r){var w=document.getElementById('w_'+r[0]);"
"if(so&&(r[0]=='int_rise'||r[0]=='int_fall'))r=[r[0],r[1],'b'];"
"if(w)w.className=(r[2]=='t'&&al!=0)||(r[2]=='i'&&al!=1)?'off':''})})}"
"showFields();"
/* «Кодов на канал»: в описании сразу виден верх шкалы - каналов x кодов */
"function cpcTop(){var c=+document.getElementById('p_cpc').value,n=+document.getElementById('p_nch').value;"
"document.getElementById('cpctop').textContent=c>0&&n?L('Сейчас шкала до ','Scale now up to ')+(+(c*n).toFixed(1))+L(' кодов.',' codes.'):''}"
"document.getElementById('p_algo').onchange=showFields;"
"document.getElementById('p_emu').onchange=setemu;"
"document.getElementById('p_cpc').oninput=cpcTop;document.getElementById('p_nch').onchange=cpcTop;"
"function setfreq(){window.REALHZ=FHZ[fs.value];fetch('/cfg?freq='+fs.value)}"
/* эмуляция MCA на COM: включается сразу, с подтверждением - консоль замолчит */
"function setemu(){var e=document.getElementById('p_emu'),v=e.value;"
"if(v!='0'&&!confirm(L('Включить эмуляцию MCA на порту UART0, ','Enable MCA emulation on UART0 at ')+v+"
"L(' бод? Сообщения консоли на этот порт идти перестанут.',' baud? Console messages will stop going to this port.')))"
"{e.value=window.EMU||'0';return}window.EMU=v;fetch('/cfg?emu='+v)}"
/* «Кодов на канал» прибор хранит в тысячных: 0.5 уходит как 500 */
"function apply(){var q=P.map(function(k){var v=document.getElementById('p_'+k).value;"
"return k=='cpc'?'cpc='+Math.round(v*1000):k+'='+v}).join('&');"
"fetch('/cfg?'+q).then(function(r){return r.json()}).then(fillCfg).catch(function(){})}"
"function fillCfg(p){if(p.fl){FHZ=p.fl;fillFq()}"
"if(p.emu!==undefined){window.EMU=String(p.emu);document.getElementById('p_emu').value=window.EMU}"
"P.forEach(function(k){document.getElementById('p_'+k).value=k=='cpc'?p[k]/1000:p[k]});"
"if(p.shape_on!==undefined){document.getElementById('sh_on').checked=p.shape_on==1;document.getElementById('sh_tol').value=p.shape_tol;"
"document.getElementById('sh_lmin').value=p.shape_lmin;document.getElementById('sh_lmax').value=p.shape_lmax;document.getElementById('sh_ln').value=p.shape_ln;window.ALGO=p.algo}"
"showFields();cpcTop()}"
"function ldcfg(){fetch('/stat').then(function(r){return r.json()}).then(function(j){window.REALHZ=j.freq})"
".catch(function(){}).then(function(){return fetch('/cfg')}).then(function(r){return r.json()})"
".then(fillCfg).catch(function(){})}"
/* форма адреса: записанная настройка, а при DHCP - то, что выдал роутер */
"function ld(){fetch('/net/status').then(r=>r.json()).then(function(j){"
"if(!window.NFILL){window.NFILL=1;var c=j.cfg;document.getElementById(c.dhcp?'nmd':'nms').checked=true;"
"if(!c.dhcp){document.getElementById('n_ip').value=c.ip;document.getElementById('n_mask').value=c.mask;"
"document.getElementById('n_gw').value=c.gw=='0.0.0.0'?'':c.gw;document.getElementById('n_dns').value=c.dns=='0.0.0.0'?'':c.dns}"
"else{document.getElementById('n_ip').value=j.eth_ip!='0.0.0.0'?j.eth_ip:'';document.getElementById('n_mask').value=j.mask;"
"document.getElementById('n_gw').value=j.gw}nmode()}})}"
/* поля адреса - только для статического */
"function nmode(){var st=document.getElementById('nms').checked;"
"document.getElementById('nst').style.opacity=st?1:.4;"
"['n_ip','n_mask','n_gw','n_dns'].forEach(function(k){document.getElementById(k).disabled=!st})}"
/* Сохранить: DHCP - сразу; статический - прибор применяет его через
   0.5 с, а страница переходит на новый адрес и оттуда подтверждает его
   (?netok=1). Не открылась за 60 с - прибор вернётся к прежнему. */
"function netSave(){var m=document.getElementById('nmsg'),st=document.getElementById('nms').checked;"
"function v(k){return document.getElementById(k).value.trim()}"
"var ip=v('n_ip'),q=st?'mode=static&ip='+encodeURIComponent(ip)+'&mask='+encodeURIComponent(v('n_mask'))+"
"'&gw='+encodeURIComponent(v('n_gw'))+'&dns='+encodeURIComponent(v('n_dns')):'mode=dhcp';"
"if(!confirm(st?L('Прибор перейдёт на адрес ','The device will move to address ')+ip+L('. Страница откроется по нему; если за 60 с не откроется, прибор вернётся к прежним настройкам. Продолжить?',"
"'. The page will open on it; if it does not open within 60 s, the device returns to the previous settings. Continue?'):"
"L('Адрес будет от роутера (DHCP) и может смениться: смотрите его на дисплее прибора или в консоли. Продолжить?',"
"'The address will come from the router (DHCP) and may change: see it on the device display or in the console. Continue?')))return;"
"fetch('/net/set?'+q).then(function(r){return r.json()}).then(function(j){m.textContent=L(j.msg,j.en||j.msg);"
"if(j.ok&&st)setTimeout(function(){location.href='http://'+ip+'/settings?netok=1'},2000)})"
".catch(function(){m.textContent=L('нет связи с прибором','no connection to the device')})}"
/* открылись с нового адреса - подтвердить его */
"if(location.search.indexOf('netok=1')>=0)fetch('/net/confirm').then(function(r){return r.json()}).then(function(j){"
"document.getElementById('nmsg').textContent=L(j.msg,j.en||j.msg)});"
"function fwi(){return fetch('/ota').then(r=>r.json()).then(function(j){"
"document.getElementById('fw').innerHTML=L('версия ','version ')+'<b>'+j.ver+'</b> ('+j.date+' '+j.time+'), '+"
"L('слот ','slot ')+j.run+(j.state=='pending'?L(', ждёт подтверждения',', awaiting confirmation'):'');return j})}"
"function fwup(){var f=document.getElementById('fwf').files[0],p=document.getElementById('fwp');"
"if(!f){alert(L('Выберите файл .bin','Choose a .bin file'));return}"
"if(!confirm(L('Записать прошивку и перезапустить прибор? Набор спектра прервётся.',"
"'Write the firmware and restart the device? Spectrum acquisition will be interrupted.')))return;"
"var x=new XMLHttpRequest();x.open('POST','/ota');"
"x.upload.onprogress=function(e){if(e.lengthComputable)p.textContent=L('передано ','sent ')+Math.round(e.loaded*100/e.total)+' %'};"
"x.onload=function(){var j={};try{j=JSON.parse(x.responseText)}catch(e){}"
"p.textContent=j.msg?L(j.msg,j.en||j.msg):x.responseText;if(j.ok)setTimeout(fww,3000)};"
"x.onerror=function(){p.textContent=L('связь прервалась','connection lost')};x.send(f)}"
/* ждём, пока прибор поднимется после перезапуска; запрос /ota подтверждает новую прошивку */
"function fww(){var p=document.getElementById('fwp');fwi().then(function(j){"
"p.textContent=L('прибор перезапущен, работает версия ','device restarted, running version ')+j.ver})"
".catch(function(){p.textContent=L('жду перезапуска прибора...','waiting for the device to restart...');setTimeout(fww,2000)})}"
"ldcfg();ld();fwi();"
/* ФИЛЬТР ФОРМЫ. /shape: 32 x int32 (набрано, нужно, p10, p50, p90 - x10,
   окно, действует, эталон x10, допуск, включён, окно эталона, частота
   эталона, частота сейчас, проверено, отброшено, столбцов гистограммы;
   эталон: длина, вершина, импульсов; два примера: длина, вершина, высота,
   ширина x10, возраст с; «до вершины»), затем гистограмма uint32, эталон
   и примеры int16. Опрос 10 раз в секунду: пока он идёт, прибор меряет форму и
   при выключенном фильтре. */
"var SH={busy:0,d:null};"
"function shSet(){var on=document.getElementById('sh_on').checked?1:0,t=+document.getElementById('sh_tol').value||25;"
"function gv(k){return Math.max(0,Math.round(+document.getElementById(k).value||0))}"
"fetch('/cfg?shape_on='+on+'&shape_tol='+t+'&shape_lmin='+gv('sh_lmin')+'&shape_lmax='+gv('sh_lmax')+'&shape_ln='+(gv('sh_ln')||2000))"
".then(function(r){return r.json()}).then(fillCfg).then(function(){shLoad()}).catch(function(){})}"
"function shLearn(){var lr=SH.d&&SH.d.need>0;"
"var nl=document.getElementById('sh_ln').value||2000;"
"if(!lr&&!confirm(L('Набрать эталон формы? Нужен запущенный набор спектра (подойдёт и фон, с источником быстрее): прибор возьмёт '+nl+' импульсов по настройке «в эталон», медиана их ширины станет эталоном и сохранится в памяти прибора. Средний импульс виден по ходу набора.',"
"'Learn the shape reference? A running spectrum acquisition is needed (background will do, a source is faster): the device takes '+nl+' pulses by the “reference” setting, the median of their width becomes the reference and is saved in the device memory. The mean pulse is shown while learning.')))return;"
"shLoad(lr?'stop':'learn')}"
"function shClr(){shLoad('clear')}"
"function shLoad(dd){if(SH.busy&&!dd)return;SH.busy=1;"
"fetch('/shape'+(dd?'?do='+dd:'')).then(function(r){return r.arrayBuffer()}).then(function(b){SH.busy=0;"
"if(b.byteLength<128)return;var v=new DataView(b),h=[],i,o;for(i=0;i<32;i++)h.push(v.getInt32(4*i,true));"
"o=128;var HB=h[15];function rd(n,sz){var a=[];for(var k=0;k<n;k++)a.push(sz==4?v.getUint32(o+4*k,true):v.getInt16(o+2*k,true));o+=n*sz;return a}"
"var d={n:h[0],need:h[1],p10:h[2],p50:h[3],p90:h[4],win:h[5],act:h[6],ref:h[7],tol:h[8],on:h[9],"
"swin:h[10],shz:h[11],hz:h[12],chk:h[13]>>>0,rej:h[14]>>>0,HB:HB,wpre:h[17],wn:h[18],rise:h[29]};"
"d.hist=rd(HB,4);d.wave=h[16]>0?rd(h[16],2):null;d.ex=[];"
"for(var k=0;k<2;k++){var q=19+5*k;d.ex.push(h[q]>0?{pre:h[q+1],h:h[q+2],w:h[q+3],age:h[q+4],v:rd(h[q],2)}:null)}"
"SH.d=d;shStat();shDraw()}).catch(function(){SH.busy=0})}"
"function f1(x){return (x/10).toFixed(1)}"
/* эталон годится для сравнения: тот же размер окна и та же частота */
"function shCmp(d){return d.ref>0&&d.swin==d.win&&d.shz==d.hz}"
"function shStat(){var d=SH.d,t=[],lb=document.getElementById('sh_lb');"
"lb.textContent=d.need>0?L('Прервать набор','Stop learning'):L('Набрать эталон','Learn reference');"
"if(d.need>0)t.push('<b style=color:#f0c45a>'+L('набор эталона: ','learning the reference: ')+d.n+L(' из ',' of ')+d.need+'</b> '+"
"L('(импульсы идут, только пока набирается спектр)','(pulses come only while the spectrum is being acquired)'));"
"if(d.ref<=0)t.push(L('эталон не набран','no reference yet'));"
"if(d.on&&!d.act)t.push('<b style=color:#ff8a82>'+(d.ref<=0?L('фильтр включён, но эталона нет - импульсы проходят все','the filter is on, but there is no reference - all pulses pass'):"
"d.swin!=d.win?L('окно «до / после вершины» сменилось после набора эталона - наберите его заново, пока фильтр пропускает всё','the “before / after peak” window changed after the reference was learned - learn it again, until then the filter passes everything'):"
"L('частота АЦП сменилась после набора эталона - наберите его заново, пока фильтр пропускает всё','the ADC rate changed after the reference was learned - learn it again, until then the filter passes everything'))+'</b>');"
"if(d.act)t.push('<b style=color:#9fe0a0>'+L('фильтр действует','filter active')+'</b>: '+L('с «Сброса» проверено ','since “Reset” checked ')+d.chk+L(', отброшено ',', rejected ')+d.rej+"
"(d.chk?' ('+(d.rej*100/d.chk).toFixed(1)+' %)':''));"
"if(window.ALGO==2&&(!shCmp(d)||!d.wave))t.push('<b style=color:#ff8a82>'+L('способ «свёртка с эталоном», но эталонного импульса для этого окна и частоты нет - амплитуда считается трапецией; наберите эталон','method “convolution with the reference”, but there is no reference pulse for this window and rate - the amplitude is computed by the trapezoid; learn the reference')+'</b>');"
"if(!d.on&&d.ref>0)t.push(L('фильтр выключен: импульсы вне допуска показаны, но в спектр идут','filter off: pulses outside the tolerance are shown but still go into the spectrum'));"
"document.getElementById('sh_st').innerHTML=t.join('<br>')}"
/* холст под размер на экране; возвращает контекст и размеры */
"function shCv(id){var c=document.getElementById(id),dpr=window.devicePixelRatio||1,W=c.clientWidth,H=c.clientHeight;"
"c.width=W*dpr;c.height=H*dpr;var x=c.getContext('2d');x.setTransform(dpr,0,0,dpr,0,0);"
"x.fillStyle='#0b0f0b';x.fillRect(0,0,W,H);x.font='10.5px monospace';return [x,W,H]}"
"var SHC={ref:'#e8efe2',ok:'#6fd38a',bad:'#ff8a82'};"
/* ЭТАЛОННЫЙ ИМПУЛЬС: средний импульс набора и последние примеры, каждый
   своей высотой к 1; по горизонтали - отсчёты от вершины. Примеры - в
   окне текущей настройки, эталон - в окне, на котором набран. */
"function shDrawWave(){var d=SH.d,g=shCv('sh_wv'),x=g[0],W=g[1],H=g[2],ml=30,mr=8,mt=8,mb=18,pw=W-ml-mr,ph=H-mt-mb;"
"var cur=[];if(d.wave)cur.push([d.wave.map(function(y){return y/1000}),d.wpre,SHC.ref,2.2]);"
"for(var k=0;k<2;k++){var e=d.ex[k];if(e&&e.h>0)cur.push([e.v.map(function(y){return y/e.h}),e.pre,k?SHC.bad:SHC.ok,1.2])}"
"var x0=-8-d.rise,x1=d.win-1-d.rise;cur.forEach(function(c){x0=Math.min(x0,-c[1]);x1=Math.max(x1,c[0].length-1-c[1])});"
"var y0=-0.2,y1=1.15;function X(t){return ml+(t-x0)/(x1-x0||1)*pw}function Y(y){return mt+(y1-y)/(y1-y0)*ph}"
"x.strokeStyle='#283024';x.lineWidth=1;[0,0.5,1].forEach(function(y){x.beginPath();x.moveTo(ml,Y(y));x.lineTo(ml+pw,Y(y));x.stroke()});"
/* окно меры: база (8 отсчётов) и «до / после вершины» */
"x.fillStyle='rgba(127,216,138,.07)';x.fillRect(X(-d.rise),mt,X(d.win-1-d.rise)-X(-d.rise),ph);"
"x.strokeStyle='#3a443a';x.setLineDash([3,3]);[-d.rise,0,d.win-1-d.rise].forEach(function(t){x.beginPath();x.moveTo(X(t),mt);x.lineTo(X(t),mt+ph);x.stroke()});x.setLineDash([]);"
"cur.slice().reverse().forEach(function(c){x.strokeStyle=c[2];x.lineWidth=c[3];x.beginPath();"
"c[0].forEach(function(y,i){var px=X(i-c[1]),py=Y(Math.max(y0,Math.min(y1,y)));if(i)x.lineTo(px,py);else x.moveTo(px,py)});x.stroke()});"
"x.lineWidth=1;x.fillStyle='#8a948a';x.fillText('1',4,Y(1)+4);x.fillText('0',4,Y(0)+4);"
"var st=Math.max(1,Math.round((x1-x0)/8/5)*5);for(var t=Math.ceil(x0/st)*st;t<=x1;t+=st)x.fillText(String(t),X(t)-6,H-5);"
"if(!cur.length){x.fillStyle='#8a948a';x.fillText(L('нет импульсов: запустите набор спектра','no pulses: start the spectrum acquisition'),ml+10,mt+20)}"
"var us=1e6/(d.hz||1),l=[];"
"l.push('<span style=color:'+SHC.ref+'>━</span> '+(d.wave?L('эталон: среднее ','reference: mean of ')+d.wn+L(' импульсов',' pulses')+(d.ref>0?', '+L('ширина ','width ')+f1(d.ref)+L(' отсч.',' samples'):''):L('эталон не набран','no reference yet')));"
"var nm=[[L('последний в допуске','last within tolerance'),SHC.ok],[L('последний вне допуска','last outside tolerance'),SHC.bad]];"
"for(k=0;k<2;k++){var e2=d.ex[k];if(k==1&&!shCmp(d))continue;"
"l.push('<span style=color:'+nm[k][1]+'>━</span> '+nm[k][0]+': '+(e2?L('высота ','height ')+e2.h+L(' кодов, ширина ',' codes, width ')+f1(e2.w)+L(' отсч.',' samples')+"
"(e2.age>2?', '+e2.age+L(' с назад',' s ago'):''):L('пока не было','none yet')))}"
"l.push(L('по горизонтали - отсчёты от вершины (1 отсч. = ','horizontally - samples from the peak (1 sample = ')+(us*1000).toFixed(0)+L(' нс), каждый импульс своей высотой к 1; светлая полоса - окно меры',' ns), each pulse scaled to its height; the light band is the measurement window'));"
"document.getElementById('sh_wl').innerHTML=l.join('<br>')}"
/* ШИРИНА ИМПУЛЬСОВ: гистограмма площадь / высота от 0 до окна, зелёным -
   в допуске, красным - вне его, полоса - эталон ± допуск */
"function shDrawHist(){var d=SH.d,g=shCv('sh_hg'),x=g[0],W=g[1],H=g[2],ml=30,mr=8,mt=8,mb=18,pw=W-ml-mr,ph=H-mt-mb;"
"var mx=0,tot=0,out=0,cmp=shCmp(d),r=d.ref/10,tl=r*d.tol/100,bw=d.win/d.HB;"
"d.hist.forEach(function(n){if(n>mx)mx=n;tot+=n});"
"function X(w){return ml+w/d.win*pw}"
"if(cmp){x.fillStyle='rgba(127,216,138,.12)';x.fillRect(X(r-tl),mt,X(r+tl)-X(r-tl),ph)}"
"d.hist.forEach(function(n,i){if(!n)return;var w=(i+0.5)*bw,ins=!cmp||(w>=r-tl&&w<=r+tl);if(!ins)out+=n;"
"var hh=Math.max(1,n/mx*ph);x.fillStyle=ins?SHC.ok:SHC.bad;x.fillRect(X(i*bw)+0.5,mt+ph-hh,Math.max(1,X(bw)-X(0)-1),hh)});"
"if(cmp){x.strokeStyle=SHC.ref;x.setLineDash([4,3]);x.beginPath();x.moveTo(X(r),mt);x.lineTo(X(r),mt+ph);x.stroke();x.setLineDash([])}"
"x.strokeStyle='#3a443a';x.strokeRect(ml+.5,mt+.5,pw,ph);x.fillStyle='#8a948a';"
"for(var k=0;k<=5;k++){var w=Math.round(d.win*k/5);x.fillText(String(w),X(w)-6,H-5)}"
"if(!tot){x.fillText(L('нет импульсов: запустите набор спектра','no pulses: start the spectrum acquisition'),ml+10,mt+20)}"
"document.getElementById('sh_hl').innerHTML=L('по горизонтали - ширина (площадь / высота), отсчётов; по высоте - сколько импульсов. ','horizontally - width (area / height), samples; vertically - how many pulses. ')+"
"(cmp?L('Полоса - эталон ± допуск, красное - вне допуска. ','The band is reference ± tolerance, red is outside the tolerance. '):'')+"
"L('С «Сброса»: ','Since “Reset”: ')+tot+L(' импульсов',' pulses')+(cmp&&tot?L(', вне допуска ',', outside the tolerance ')+out+' ('+(out*100/tot).toFixed(1)+' %)':'')}"
"function shDraw(){if(SH.d){shDrawWave();shDrawHist()}}"
/* 10 раз в секунду; пока ответ не пришёл, новый запрос не уходит (SH.busy) -
   при медленной сети карточка сама обновляется реже */
"shLoad();setInterval(function(){shLoad()},100);window.addEventListener('resize',shDraw);"
"</script></body></html>";

static esp_err_t h_set_page(httpd_req_t *r)
{
    httpd_resp_set_type(r, "text/html; charset=utf-8");
    return httpd_resp_send(r, SET_PAGE, HTTPD_RESP_USE_STRLEN);
}

/* ---- страница «Диагностика»: лог прибора и диагностика тракта АЦП ---- */
static const char DIAG_PAGE[] =
SUB_HEAD("Диагностика", "Diagnostics") SUB_NAV(N_SET, N_DIAG_ON, N_HELP)
"<div class=wrap>"
/* лог прибора: строки ESP_LOG с начала работы (кольцо 256 строк, mca_log.c),
   цвет - по уровню, фильтр по уровням, файл - кнопкой */
"<section class='panel pad'>"
"<div class=grp style=margin-bottom:8px><b style=color:#e8efe2 data-en='Device log'>Лог прибора</b>"
"<span class=ml></span>"
"<button class='btn sm' onclick=lgsave() data-en='Save'>Сохранить</button>"
"<button class='btn sm' onclick=lgclr() data-en='Clear screen'>Очистить экран</button></div>"
/* одна строка фильтров: уровни словами | подсистемы (их рисует lgcats) | следить */
"<div class=grp style='margin-bottom:8px;gap:6px 14px'>"
"<label><input type=checkbox id=lgE checked onchange=lgdraw()> <b style=color:#ff8a82 data-en='Errors'>Ошибки</b></label>"
"<label><input type=checkbox id=lgW checked onchange=lgdraw()> <b style=color:#f0c45a data-en='Warnings'>Предупреждения</b></label>"
"<label><input type=checkbox id=lgI checked onchange=lgdraw()> <b style=color:#cfe0cc data-en='Messages'>Сообщения</b></label>"
"<label><input type=checkbox id=lgD checked onchange=lgdraw()> <b style=color:#7d8a7b data-en='Debug'>Отладка</b></label>"
"<span style='border-left:1px solid #3a443a;align-self:stretch'></span>"
"<span id=lgcats style=display:contents></span>"
"<label class=ml title='прокручивать к новым строкам' data-en-title='scroll to new lines'>"
"<input type=checkbox id=lgf checked> <span data-en='follow'>следить</span></label></div>"
"<div id=lgv class=mono style='height:420px;overflow:auto;font-size:12px;line-height:1.45;"
"background:#0b0f0b;border:1px solid #283024;border-radius:6px;padding:6px 8px;"
"white-space:pre-wrap;word-break:break-word'></div>"
"<div class=mut id=lgs style=font-size:11.5px;margin-top:6px></div>"
"</section>"
"<section class='panel pad' id=g_diag>"
"<h3 data-en='ADC diagnostics'>Диагностика АЦП</h3>"
"<label><input type=checkbox id=den onchange=dtog()> "
"<span data-en='per-bit statistics (analyses 1 chunk/s)'>побитовая статистика (разбор 1 чанка/с)</span></label>"
"<div id=dout></div>"
"<button class='btn sm' onclick=dprobe() style=margin-top:10px data-en='Check data lines'>Проверить линии данных</button>"
"<div id=dpr style=margin-top:8px;font-size:12.5px></div></section>"
"</div>"
"<script>i18n();"
/* /log?from=N: первая строка - «#следующий самый_старый», дальше «номер
   уровень текст». Номер следующего меньше прежнего - прибор перезапущен. */
"var LG={nx:0,a:[],busy:0,lost:0},LGC={E:'#ff8a82',W:'#f0c45a',I:'#cfe0cc',D:'#7d8a7b',V:'#5e6a5d'};"
/* подсистема - по тегу строки ESP_LOG; у каждой свой цвет тега и своя галочка */
"var LGK=[['adc',L('АЦП','ADC'),'#6cc4ff',/^(adc_|mca_dsp|mca_diag|prof|gpio|cam|gdma|lcd)/],"
"['net',L('Сеть','Network'),'#b896ff',/^(mca_eth|esp_eth|esp_netif|netif|w5500|eth|httpd|mca_web|mca_time|sntp|lwip|dhcp|ws)/],"
"['fw',L('Прошивка','Firmware'),'#ff9d5c',/^(ota|esp_ota|boot|app_|esp_image|spi_flash)/],"
"['cfg',L('Настройки','Settings'),'#4fd6ae',/^(settings|nvs|mca_hist)/],"
"['oled',L('Дисплей','Display'),'#ef8fd6',/^(oled|i2c)/],"
"['emu',L('Эмуляция','Emulation'),'#a6dc6a',/^mca_emu/],"
"['sys',L('Система','System'),'#d4c08c',/./]];"
"function lgcat(t){for(var i=0;i<LGK.length;i++)if(LGK[i][3].test(t))return i;return LGK.length-1}"
"function lgcats(){var h='',n=LGK.map(function(){return 0});LG.a.forEach(function(x){n[x[2]]++});"
"LGK.forEach(function(k,i){var c=document.getElementById('lc'+k[0]);"
"h+='<label><input type=checkbox id=lc'+k[0]+((c&&!c.checked)?'':' checked')+' onchange=lgdraw()> '+"
"'<b style=color:'+k[2]+'>'+k[1]+'</b> <span class=mut>'+n[i]+'</span></label>'});"
"document.getElementById('lgcats').innerHTML=h}"
/* «E (12345) тег: текст» -> [уровень, исходная строка, подсистема, время, тег, текст] */
"function lgparse(lv,t){var m=/^[EWIDV] \\((\\d+)\\) ([^:]{1,40}): ?(.*)$/.exec(t);"
"if(!m)return [lv,t,LGK.length-1,'','',t];return [lv,t,lgcat(m[2]),m[1],m[2],m[3]]}"
"function lgload(){if(LG.busy)return;LG.busy=1;"
"fetch('/log?from='+LG.nx).then(function(r){return r.text()}).then(function(t){LG.busy=0;"
"var ls=t.split('\\n'),h=ls[0].slice(1).split(' '),nx=+h[0],first=+h[1],add=0;"
"if(nx<LG.nx){LG.a=[];LG.a.push(lgparse('W',L('— прибор перезапущен —','— device restarted —')))}"
"else if(LG.nx>0&&first>LG.nx)LG.lost+=first-LG.nx;"
"for(var i=1;i<ls.length;i++){var x=ls[i];if(!x)continue;var p=x.indexOf(' ');"
"LG.a.push(lgparse(x.charAt(p+1),x.slice(p+3)));add++}"
"if(LG.a.length>3000)LG.a.splice(0,LG.a.length-3000);LG.nx=nx;"
"document.getElementById('lgs').textContent=L('строк на экране: ','lines on screen: ')+LG.a.length+"
"(LG.lost?L(', пропущено (кольцо переполнилось): ',', skipped (the ring overflowed): ')+LG.lost:'');"
"if(add){lgcats();lgdraw()}}).catch(function(){LG.busy=0})}"
"function lgesc(x){return x.replace(/&/g,'&amp;').replace(/</g,'&lt;')}"
/* в тексте обычных строк: IP, 0x.., числа с единицами - голубым */
"function lgnum(x){return lgesc(x).replace(/(\\b\\d{1,3}(\\.\\d{1,3}){3}\\b|\\b0x[0-9a-fA-F]+\\b|-?\\b\\d+([.,]\\d+)?( ?(%|МГц|MHz|кГц|kHz|мкс|мс|ms|us|КБ|KB|Б|B|с|s)(?![A-Za-zА-Яа-яЁё]))?)/g,"
"'<span style=color:#8fd8d2>$1</span>')}"
"function lgdraw(){var v=document.getElementById('lgv'),h='';"
"LG.a.forEach(function(x){var c=document.getElementById('lg'+x[0]);if(c&&!c.checked)return;"
"var k=LGK[x[2]],kc=document.getElementById('lc'+k[0]);if(kc&&!kc.checked)return;"
"var lc=LGC[x[0]]||'#9aa59a';"
"if(!x[4]){h+='<div style=color:'+lc+'>'+lgesc(x[1])+'</div>';return}"
"var bg=x[0]=='E'?'background:#3a1716;':x[0]=='W'?'background:#2e2812;':'';"
"h+='<div style=\"'+bg+'color:'+lc+'\"><b style=color:'+lc+'>'+x[0]+'</b> '+"
"'<span style=color:#5f6b5e>'+(x[3]/1000).toFixed(3).padStart(8,'\\u00a0')+'</span> '+"
"'<span style=color:'+k[2]+'>'+lgesc(x[4])+'</span> '+(x[0]=='E'||x[0]=='W'?lgesc(x[5]):lgnum(x[5]))+'</div>'});"
"v.innerHTML=h;if(document.getElementById('lgf').checked)v.scrollTop=v.scrollHeight}"
"function lgclr(){LG.a=[];LG.lost=0;lgcats();lgdraw()}"
"function lgsave(){var s=LG.a.map(function(x){return x[1]}).join('\\r\\n')+'\\r\\n',n=new Date(),"
"p=function(v){return (v<10?'0':'')+v},a=document.createElement('a');"
"a.href=URL.createObjectURL(new Blob([s],{type:'text/plain'}));"
"a.download='mca_log_'+n.getFullYear()+p(n.getMonth()+1)+p(n.getDate())+'_'+p(n.getHours())+p(n.getMinutes())+p(n.getSeconds())+'.txt';"
"document.body.appendChild(a);a.click();setTimeout(function(){URL.revokeObjectURL(a.href);a.remove()},1000)}"
/* диагностика тракта АЦП: опрос раз в секунду, пока страница открыта */
"function dtog(){fetch('/diag/set?en='+(document.getElementById('den').checked?1:0))}"
"function df(v,c){return '<span class='+c+'>'+v+'</span>'}"
/* строка таблицы: подпись и значение */
"function dR(a,b){return '<tr><td>'+a+'</td><td>'+b+'</td></tr>'}"
"function dupd(){fetch('/diag/data').then(r=>r.json()).then(function(j){"
"document.getElementById('den').checked=j.enabled;"
"var h='<table class=kv><tr><th>'+L('Параметр','Parameter')+'</th><th>'+L('Значение','Value')+'</th></tr>';"
"h+=dR(L('Поток данных','Data flow'),j.flow?df(L('идёт','flowing'),'ok'):df(L('НЕТ ДАННЫХ','NO DATA'),'bad'));"
"h+=dR(L('Чанков всего','Chunks total'),j.chunks);"
"h+=dR(L('Отсчётов всего','Samples total'),j.samples);"
"var ec=Math.abs(j.err)<=2?'ok':(Math.abs(j.err)<=10?'warn':'bad');"
"h+=dR(L('Скорость измеренная','Measured rate'),(j.rate/1e6).toFixed(3)+L(' МГц',' MHz'));"
"h+=dR(L('Скорость ожидаемая','Expected rate'),(j.exp/1e6).toFixed(3)+L(' МГц',' MHz'));"
"h+=dR(L('Отклонение','Deviation'),df(j.err+' %',ec));"
"h+=dR(L('Потеряно чанков','Chunks lost'),j.lost>0?df(j.lost,'warn'):j.lost);"
"var tot=j.chunks+j.lost;var lp=tot?100*j.lost/tot:0;"
"h+=dR(L('Доля потерь','Loss share'),lp<0.1?df(lp.toFixed(2)+L(' % — чисто',' % — clean'),'ok'):"
"df(lp.toFixed(1)+L(' % — обработка не успевает',' % — processing cannot keep up'),'bad'));"
"if(lp>0.1){var can=j.exp*(1-lp/100);"
"h+=dR(L('Потолок обработки','Processing ceiling'),df(L('около ','about ')+(can/1e6).toFixed(1)+"
"L(' МГц — поставьте эту частоту или ниже',' MHz — set this rate or lower'),'warn'))}"
"var cs=(j.ctrl>>>29)&3;var st=(j.ctrl1>>>29)&1;var bl=j.ctrl1&0xFFFF;"
"var tb=(j.ctrl1>>>24)&1;"
"h+=dR(L('Такт модуля CAM','CAM module clock'),cs?df(L('вкл','on')+' (clk_sel='+cs+')','ok'):df(L('ВЫКЛЮЧЕН','OFF'),'bad'));"
"h+=dR('cam_start',st?df(L('запущен','started'),'ok'):df(L('НЕ УСТАНОВЛЕН','NOT SET'),'bad'));"
"h+=dR(L('16-битный режим','16-bit mode'),tb?df(L('да','yes'),'ok'):df(L('нет','no'),'bad'));"
"h+=dR(L('Длина чанка, байт','Chunk length, bytes'),bl+1);"
"h+=dR(L('Уровень на PCLK','PCLK level'),j.pclk);"
"h+='<tr><td>cam_ctrl / ctrl1</td><td>0x'+(j.ctrl>>>0).toString(16)+' / 0x'+(j.ctrl1>>>0).toString(16)+'</td></tr>';"
"h+='</table>';"
/* ПРОФИЛЬ: что было за прошлую секунду. Очередь 16 из 16 - следующий
   чанк потерян; долгие запросы - кандидаты в виновники. */
"var p=j.pf;if(p){"
"var qc=p.q>=14?'bad':(p.q>=8?'warn':'ok');"
"h+='<h4>'+L('Профиль за последнюю секунду','Profile for the last second')+'</h4><table class=kv><tr><th>'+"
"L('Что','What')+'</th><th>'+L('Значение','Value')+'</th></tr>';"
"h+=dR(L('Чанков обработано','Chunks processed'),p.ch);"
"h+=dR(L('Пик очереди чанков','Chunk queue peak'),df(p.q+L(' из 16',' of 16'),qc)+"
"' <span style=opacity:.6>'+L('(16 - следующий теряется)','(16 - the next one is lost)')+'</span>');"
"h+=dR(L('Самая долгая обработка чанка','Longest chunk processing'),(p.work/1000).toFixed(2)+L(' мс',' ms'));"
"h+=dR(L('Самое долгое ожидание чанка','Longest chunk wait'),(p.wait/1000).toFixed(2)+L(' мс',' ms'));"
"h+=dR(L('Потеряно за секунду','Lost in the last second'),p.lost?df(p.lost,'bad'):'0');"
"var wn=['/spectrum','/scope','/stat'];"
"for(var k=0;k<3;k++)h+=dR(L('Запрос ','Request ')+wn[k],p.wcnt[k]+L(' за с, самый долгий ','/s, longest ')+"
"(p.wmax[k]/1000).toFixed(1)+L(' мс',' ms'));"
"h+=dR(L('Свободно внутр. памяти','Free internal memory'),(p.heap/1024).toFixed(0)+L(' КБ (минимум ',' KB (minimum ')+"
"(p.hmin/1024).toFixed(0)+L(' КБ)',' KB)'));"
/* загрузка обработки на ядре 1 - доля времени в работе с чанками (та же,
   что в строке состояния спектра; около 100 % - потери), ядро 0 - сеть,
   веб, дисплей и прерывание захвата (1000 минус доля задачи простоя), и
   самые занятые задачи, кроме обработки: у неё FreeRTOS считает и
   засыпание-пробуждение на каждом чанке, которого под нагрузкой нет */
"if(j.cores){var cl=function(v){return df((v/10).toFixed(1)+' %',v>950?'bad':(v>850?'warn':'ok'))};"
"h+=dR(L('Загрузка ядра 0 (сеть, веб, дисплей)','Core 0 load (network, web, display)'),cl(j.cores[0])+"
"(j.isr!==undefined?' <span style=opacity:.6>'+L('+ прерывание захвата ','+ capture interrupt ')+(j.isr/10).toFixed(1)+' %</span>':''));"
"h+=dR(L('Загрузка обработки (ядро 1)','Processing load (core 1)'),cl(p.busy));"
"var tk=(j.tasks||[]).filter(function(t){return t[0]!='dsp'});"
"if(tk.length)h+=dR(L('Самые занятые задачи','Busiest tasks'),tk.map(function(t){"
"return t[0]+' <span style=opacity:.6>('+L('ядро ','core ')+(t[1]==255?L('любое','any'):t[1])+')</span> '+(t[2]/10).toFixed(1)+' %'}).join(', '))}"
/* такты процессора на отсчёт по этапам против бюджета 240 МГц / частота:
   видно, хватает ли ядра на эту частоту и что в обработке дороже всего */
"if(j.cyc&&j.fhz){var bud=240e6/j.fhz,cs=(j.cyc[0]+j.cyc[1]+j.cyc[2])/100;"
"h+=dR(L('Такты на отсчёт (спектр)','CPU cycles per sample (spectrum)'),cs>0?"
"L('копия ','copy ')+(j.cyc[0]/100).toFixed(2)+L(' + разность ',' + difference ')+(j.cyc[1]/100).toFixed(2)+"
"L(' + порог и события ',' + threshold and events ')+(j.cyc[2]/100).toFixed(2)+' = '+"
"df(cs.toFixed(2),cs>bud*0.9?'bad':(cs>bud*0.75?'warn':'ok'))+L(' из ',' of ')+bud.toFixed(1):"
"L('спектр сейчас не обрабатывается','the spectrum is not being processed now'))}"
"h+='</table>';"
/* Журнал потерь: что делал веб в момент потери чанка. Если потери
   совпадают с запросами - виноват веб, если нет - обработка. */
"var wn2=[L('нет','none'),'/spectrum','/scope','/stat'];"
"if(j.ev&&j.ev.length){"
"h+='<h4>'+L('Журнал потерь чанков','Chunk loss log')+'</h4><table><tr><th>'+L('Время','Time')+'</th><th>'+"
"L('Потеряно','Lost')+'</th><th>'+L('Очередь','Queue')+'</th><th>'+L('Шёл запрос','Request in progress')+'</th><th>'+"
"L('Длился','Took')+'</th><th>'+L('Обработка','Processing')+'</th><th>'+L('Режим','Mode')+'</th></tr>';"
"j.ev.forEach(function(e){"
"h+='<tr><td>'+(e[0]/1000).toFixed(1)+L(' с',' s')+'</td><td>'+e[1]+'</td><td>'+e[2]+'</td>'+"
"'<td>'+(wn2[e[3]]||'?')+'</td><td>'+(e[4]/1000).toFixed(1)+L(' мс',' ms')+'</td><td>'+"
"(e[5]/1000).toFixed(2)+L(' мс',' ms')+'</td><td>'+"
"(e[6]==1||e[6]==3?L('осциллограф','scope'):L('спектр','spectrum'))+'</td></tr>'});"
"h+='</table>'}"
"else h+='<p style=opacity:.6>'+L('Потерь чанков не было.','No chunk losses.')+'</p>';"
"}"
"if(j.stats){"
"h+='<table><tr><th>'+L('Бит','Bit')+'</th><th>'+L('Переключений','Toggles')+'</th><th>'+L('Состояние','State')+'</th></tr>';"
"var nm=['D0','D1','D2','D3','D4','D5','D6','D7','D8','D9','D10','D11'];"
"for(var b=0;b<12;b++){var st='',c='ok';"
"if(j.a1&(1<<b)){st=L('всегда 1','always 1');c='bad'}"
"else if(j.a0&(1<<b)){st=L('всегда 0','always 0');c='bad'}"
"else st=L('меняется','toggling');"
"h+='<tr><td>'+nm[b]+'</td><td>'+j.tg[b]+'</td><td>'+df(st,c)+'</td></tr>'}"
"h+='</table>';"
"h+='<table class=kv><tr><th>'+L('Значения в чанке','Values in the chunk')+'</th><th></th></tr>';"
"h+=dR('min / max',j.vmin+' / '+j.vmax);"
"h+=dR(L('среднее','mean'),j.vmean);"
"h+=dR(L('размах','range'),j.vmax-j.vmin);"
/* Линии 12-15 камерного интерфейса прибиты к постоянному нулю: единицы
   там значат, что ноль не ноль (так было с неверной константой матрицы) */
"h+=dR(L('старшие биты 12-15 (должны быть 0)','high bits 12-15 (must be 0)'),"
"j.hib>0?df(j.hib+L(' отсчётов - константа нуля не работает, обработка будет врать',"
"' samples - the zero constant does not work, processing will be wrong'),'bad'):df('0','ok'));"
"h+=dR(L('проанализировано','analysed'),j.an+L(' отсчётов',' samples'));"
"h+='</table>'}"
"else h+='<p style=opacity:.6>'+L('Побитовая статистика выключена.','Per-bit statistics are off.')+'</p>';"
"document.getElementById('dout').innerHTML=h})}"
"function dprobe(){document.getElementById('dpr').textContent=L('измеряю 4 с...','measuring 4 s...');"
"fetch('/diag/probe').then(r=>r.json()).then(function(j){"
"var nm=['D0','D1','D2','D3','D4','D5','D6','D7','D8','D9','D10','D11','OTR'];"
"var h='<table class=kv><tr><th>'+L('Линия','Line')+'</th><th>'+L('Состояние','State')+'</th></tr>';var live=0;"
"for(var b=0;b<13;b++){var st,c;"
"if(j.ch&(1<<b)){st=L('меняется','toggling');c='ok';live++}"
"else if(j.hi&(1<<b)){st=L('залипла в 1','stuck at 1');c='bad'}"
"else st=L('залипла в 0','stuck at 0'),c='bad';"
"h+='<tr><td>'+nm[b]+'</td><td>'+df(st,c)+'</td></tr>'}h+='</table>';"
"h+=live?df(L('Активных линий: ','Active lines: ')+live+L(' — АЦП преобразует',' — the ADC is converting'),'ok'):"
"df(L('НИ ОДНА линия не шевелится — АЦП не преобразует. Смотрите питание платы и приходит ли на неё такт.',"
"'NOT A SINGLE line is changing — the ADC is not converting. Check the board power and whether the clock reaches it.'),'bad');"
"document.getElementById('dpr').innerHTML=h})}"
"lgcats();lgload();setInterval(lgload,1000);dupd();setInterval(dupd,1000);"
"</script></body></html>";

static esp_err_t h_diag_page(httpd_req_t *r)
{
    httpd_resp_set_type(r, "text/html; charset=utf-8");
    return httpd_resp_send(r, DIAG_PAGE, HTTPD_RESP_USE_STRLEN);
}

/* /log?from=N - строки лога прибора с номера N (см. mca_log.h): первая
 * строка «#следующий самый_старый», дальше «номер уровень текст». N вне
 * кольца - с самой старой из лежащих. */
static esp_err_t h_log(httpd_req_t *r)
{
    char q[32], v[12];
    uint32_t from = 0, first, next;
    if (httpd_req_get_url_query_str(r, q, sizeof(q)) == ESP_OK &&
        httpd_query_key_value(q, "from", v, sizeof(v)) == ESP_OK)
        from = (uint32_t)strtoul(v, NULL, 10);
    mca_log_range(&first, &next);
    if (from < first || from > next) from = first;
    httpd_resp_set_type(r, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    /* веб-сервер обрабатывает запросы по одному - общий буфер безопасен */
    static char buf[2048];
    static char txt[MCA_LOG_WIDTH];
    int n = snprintf(buf, sizeof(buf), "#%lu %lu\n", (unsigned long)next,
                     (unsigned long)first);
    esp_err_t e = ESP_OK;
    for (uint32_t i = from; i < next && e == ESP_OK; i++) {
        char lv;
        if (!mca_log_get(i, &lv, txt, sizeof(txt))) continue;
        if (n + (int)strlen(txt) + 16 > (int)sizeof(buf)) {
            e = httpd_resp_send_chunk(r, buf, n);
            n = 0;
        }
        n += snprintf(buf + n, sizeof(buf) - n, "%lu %c %s\n", (unsigned long)i, lv, txt);
    }
    if (e == ESP_OK && n) e = httpd_resp_send_chunk(r, buf, n);
    if (e == ESP_OK) e = httpd_resp_send_chunk(r, NULL, 0);
    return e;
}

/* /net/set?mode=dhcp | /net/set?mode=static&ip=&mask=&gw=&dns= - настройка
 * адреса. Статический применяется через 0.5 с (ответ успевает уйти) и
 * ждёт подтверждения /net/confirm с нового адреса 60 с, иначе прибор
 * вернёт прежнюю. Сообщение - на двух языках: msg и en. */
#define NET_CONFIRM_S 60

static bool q_ip(const char *q, const char *key, uint32_t *out)
{
    char v[20];
    esp_ip4_addr_t a;
    if (httpd_query_key_value(q, key, v, sizeof(v)) != ESP_OK || !v[0]) return false;
    if (esp_netif_str_to_ip4(v, &a) != ESP_OK) return false;
    *out = a.addr;
    return true;
}

static esp_err_t net_reply(httpd_req_t *r, bool ok, const char *msg, const char *en)
{
    char buf[400];
    int n = snprintf(buf, sizeof(buf), "{\"ok\":%s,\"msg\":\"%s\",\"en\":\"%s\",\"t\":%d}",
                     ok ? "true" : "false", msg, en, NET_CONFIRM_S);
    if (n > (int)sizeof(buf) - 1) n = (int)sizeof(buf) - 1;
    httpd_resp_set_type(r, "application/json");
    return httpd_resp_send(r, buf, n);
}

static esp_err_t h_net_set(httpd_req_t *r)
{
    char q[200], v[12] = { 0 };
    if (httpd_req_get_url_query_str(r, q, sizeof(q)) != ESP_OK ||
        httpd_query_key_value(q, "mode", v, sizeof(v)) != ESP_OK)
        return net_reply(r, false, "нет параметра mode", "no mode parameter");
    mca_net_cfg_t c = { 0 };
    if (!strcmp(v, "dhcp")) {
        c.dhcp = 1;
    } else {
        if (!q_ip(q, "ip", &c.ip) || !q_ip(q, "mask", &c.mask))
            return net_reply(r, false, "адрес или маска заданы неверно",
                             "the address or mask is invalid");
        /* маска - сплошные единицы слева: 255.255.255.0 и т.п. */
        const uint32_t m = __builtin_bswap32(c.mask);
        if (m == 0 || (~m & (~m + 1)) != 0)
            return net_reply(r, false, "маска задана неверно", "the mask is invalid");
        q_ip(q, "gw", &c.gw);
        q_ip(q, "dns", &c.dns);
        if (c.gw && (c.gw & c.mask) != (c.ip & c.mask))
            return net_reply(r, false, "шлюз не из этой подсети",
                             "the gateway is not in this subnet");
    }
    if (mca_eth_set_cfg(&c, 500, NET_CONFIRM_S) != ESP_OK)
        return net_reply(r, false, "Ethernet не работает", "Ethernet is not working");
    return c.dhcp ? net_reply(r, true, "адрес будет от роутера (DHCP)",
                              "the address will come from the router (DHCP)")
                  : net_reply(r, true, "адрес применён, жду подтверждения",
                              "address applied, waiting for confirmation");
}

static esp_err_t h_net_confirm(httpd_req_t *r)
{
    return mca_eth_confirm()
        ? net_reply(r, true, "новый адрес подтверждён и сохранён",
                    "the new address is confirmed and saved")
        : net_reply(r, false, "подтверждать нечего", "nothing to confirm");
}

/* /oled.pbm - то, что сейчас на дисплее SSD1306, картинкой PBM 128x64
 * (рисуется и без самого дисплея) */
static esp_err_t h_oled(httpd_req_t *r)
{
    static uint8_t buf[16 + 128 * 64 / 8];
    const size_t n = mca_oled_pbm(buf, sizeof(buf));
    httpd_resp_set_type(r, "image/x-portable-bitmap");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    return httpd_resp_send(r, (const char *)buf, n);
}

static esp_err_t h_net_status(httpd_req_t *r)
{
    uint32_t ip, mask, gw;
    mca_eth_ip_info(&ip, &mask, &gw);
    mca_net_cfg_t cur, sv;
    mca_eth_get_cfg(&cur, &sv);
    const esp_ip4_addr_t eip = { .addr = ip }, em = { .addr = mask }, eg = { .addr = gw },
                         cip = { .addr = sv.ip }, cm = { .addr = sv.mask },
                         cg = { .addr = sv.gw }, cd = { .addr = sv.dns };
    char buf[480];
    /* cfg - записанная настройка (её показывает форма), pend - секунд до
     * возврата прежней, пока новый адрес не подтверждён */
    int n = snprintf(buf, sizeof(buf),
        "{\"eth_present\":%s,\"eth_link\":%s,\"eth_ip\":\"" IPSTR "\","
        "\"mask\":\"" IPSTR "\",\"gw\":\"" IPSTR "\",\"dhcp\":%d,\"pend\":%d,"
        "\"cfg\":{\"dhcp\":%d,\"ip\":\"" IPSTR "\",\"mask\":\"" IPSTR "\","
        "\"gw\":\"" IPSTR "\",\"dns\":\"" IPSTR "\"}}",
        mca_eth_present() ? "true" : "false",
        mca_eth_link_up() ? "true" : "false", IP2STR(&eip), IP2STR(&em), IP2STR(&eg),
        cur.dhcp, mca_eth_pending_s(), sv.dhcp, IP2STR(&cip), IP2STR(&cm),
        IP2STR(&cg), IP2STR(&cd));
    httpd_resp_set_type(r, "application/json");
    /* snprintf возвращает длину, которая ПОТРЕБОВАЛАСЬ БЫ. При нехватке
     * места это больше размера буфера, и отправка читала бы за его
     * границей. Ограничиваем. */
    if (n > (int)sizeof(buf) - 1) n = (int)sizeof(buf) - 1;
    return httpd_resp_send(r, buf, n);
}

static esp_err_t h_diag_data(httpd_req_t *r)
{
    mca_diag_t d;
    mca_diag_get(&d);

    uint32_t reg_ctrl = 0, reg_ctrl1 = 0;
    int pclk_lvl = -1;
    adc_cap_dump_regs(&reg_ctrl, &reg_ctrl1, &pclk_lvl);

    static char buf[3000];              /* запросы по одному - общий буфер */
    int n = snprintf(buf, sizeof(buf),
        "{\"enabled\":%s,\"flow\":%s,\"chunks\":%llu,\"samples\":%llu,"
        "\"rate\":%lu,\"exp\":%lu,\"err\":%ld,\"lost\":%llu,"
        "\"ctrl\":%lu,\"ctrl1\":%lu,\"pclk\":%d,\"stats\":%s",
        mca_diag_get_enabled() ? "true" : "false",
        d.data_flowing ? "true" : "false",
        (unsigned long long)d.chunks_total,
        (unsigned long long)d.samples_total,
        (unsigned long)d.rate_measured, (unsigned long)d.rate_expected,
        (long)d.rate_error_pct,
        (unsigned long long)adc_cap_chunks_lost(),
        (unsigned long)reg_ctrl, (unsigned long)reg_ctrl1, pclk_lvl,
        d.stats_valid ? "true" : "false");

    if (d.stats_valid) {
        n += snprintf(buf + n, sizeof(buf) - n,
            ",\"a1\":%u,\"a0\":%u,\"vmin\":%u,\"vmax\":%u,\"vmean\":%lu,"
            "\"hib\":%lu,\"an\":%lu,\"tg\":[",
            d.bit_always_1, d.bit_always_0, d.vmin, d.vmax,
            (unsigned long)d.vmean, (unsigned long)d.hibits_count,
            (unsigned long)d.analyzed_samples);
        for (int b = 0; b < DIAG_NBITS; b++)
            n += snprintf(buf + n, sizeof(buf) - n, "%s%lu",
                          b ? "," : "", (unsigned long)d.bit_toggles[b]);
        n += snprintf(buf + n, sizeof(buf) - n, "]");
    }

    /* ПРОФИЛЬ за прошлую секунду и журнал последних потерь (mca_prof) */
    mca_prof_t pf;
    mca_prof_get(&pf);
    n += snprintf(buf + n, sizeof(buf) - n,
        ",\"pf\":{\"ch\":%lu,\"q\":%lu,\"wait\":%lu,\"work\":%lu,\"busy\":%lu,"
        "\"lost\":%lu,\"heap\":%lu,\"hmin\":%lu,"
        "\"wmax\":[%lu,%lu,%lu],\"wcnt\":[%lu,%lu,%lu]}",
        (unsigned long)pf.chunks, (unsigned long)pf.q_max,
        (unsigned long)pf.wait_max_us, (unsigned long)pf.work_max_us,
        (unsigned long)pf.busy_pm,
        (unsigned long)pf.lost_1s, (unsigned long)pf.heap_now,
        (unsigned long)pf.heap_min,
        (unsigned long)pf.web_max_us[PROF_EP_SPEC],
        (unsigned long)pf.web_max_us[PROF_EP_SCOPE],
        (unsigned long)pf.web_max_us[PROF_EP_STAT],
        (unsigned long)pf.web_cnt[PROF_EP_SPEC],
        (unsigned long)pf.web_cnt[PROF_EP_SCOPE],
        (unsigned long)pf.web_cnt[PROF_EP_STAT]);
    /* загрузка ядер и самые занятые задачи: [имя, ядро (255 - любое), промилле] */
    n += snprintf(buf + n, sizeof(buf) - n, ",\"cores\":[%lu,%lu],\"isr\":%lu,\"tasks\":[",
                  (unsigned long)pf.core_pm[0], (unsigned long)pf.core_pm[1],
                  (unsigned long)pf.isr_pm);
    for (uint32_t i = 0; i < pf.ntasks && n < (int)sizeof(buf) - 64; i++)
        n += snprintf(buf + n, sizeof(buf) - n, "%s[\"%s\",%u,%u]", i ? "," : "",
                      pf.tasks[i].name, (unsigned)pf.tasks[i].core, (unsigned)pf.tasks[i].pm);
    n += snprintf(buf + n, sizeof(buf) - n, "]");

    /* Такты на отсчёт по этапам обработки спектра (x100) и частота АЦП:
     * страница сравнивает их с бюджетом 240 МГц / частота. */
    mca_stats_t st;
    mca_dsp_get_stats(&st);
    n += snprintf(buf + n, sizeof(buf) - n,
        ",\"cyc\":[%lu,%lu,%lu],\"fhz\":%lu",
        (unsigned long)st.cyc_x100[0], (unsigned long)st.cyc_x100[1],
        (unsigned long)st.cyc_x100[2], (unsigned long)adc_clk_get_freq());

    mca_prof_ev_t ev[12];
    size_t ne = mca_prof_events(ev, 12);
    n += snprintf(buf + n, sizeof(buf) - n, ",\"ev\":[");
    for (size_t i = 0; i < ne && n < (int)sizeof(buf) - 96; i++)
        n += snprintf(buf + n, sizeof(buf) - n,
                      "%s[%lu,%lu,%lu,%u,%lu,%lu,%u]", i ? "," : "",
                      (unsigned long)ev[i].t_ms, (unsigned long)ev[i].lost,
                      (unsigned long)ev[i].q_depth, (unsigned)ev[i].web_ep,
                      (unsigned long)ev[i].web_us,
                      (unsigned long)ev[i].work_us, (unsigned)ev[i].mode);
    n += snprintf(buf + n, sizeof(buf) - n, "]}");

    httpd_resp_set_type(r, "application/json");
    /* snprintf возвращает длину, которая ПОТРЕБОВАЛАСЬ БЫ. При нехватке
     * места это больше размера буфера, и отправка читала бы за его
     * границей. Ограничиваем. */
    if (n > (int)sizeof(buf) - 1) n = (int)sizeof(buf) - 1;
    return httpd_resp_send(r, buf, n);
}

static esp_err_t h_diag_probe(httpd_req_t *r)
{
    uint32_t hi = 0, lo = 0;
    uint32_t ch = adc_cap_probe_data_lines(&hi, &lo);
    char buf[192];
    int n = snprintf(buf, sizeof(buf),
                     "{\"ch\":%lu,\"hi\":%lu,\"lo\":%lu}",
                     (unsigned long)ch, (unsigned long)hi,
                     (unsigned long)lo);
    httpd_resp_set_type(r, "application/json");
    /* snprintf возвращает длину, которая ПОТРЕБОВАЛАСЬ БЫ. При нехватке
     * места это больше размера буфера, и отправка читала бы за его
     * границей. Ограничиваем. */
    if (n > (int)sizeof(buf) - 1) n = (int)sizeof(buf) - 1;
    return httpd_resp_send(r, buf, n);
}


static esp_err_t h_diag_set(httpd_req_t *r)
{
    char q[32], v[8];
    if (httpd_req_get_url_query_str(r, q, sizeof(q)) == ESP_OK &&
        httpd_query_key_value(q, "en", v, sizeof(v)) == ESP_OK) {
        mca_diag_set_enabled(atoi(v) != 0);
    }
    return httpd_resp_sendstr(r, "ok");
}


/* ---- справка по параметрам ---- */
static const char HELP_PAGE[] =
SUB_HEAD("Справка", "Help") SUB_NAV(N_SET, N_DIAG, N_HELP_ON)
"<style>td:first-child{white-space:nowrap;font-weight:600;width:1%}"
"table{margin:4px 0 14px}</style>"
"<div class=wrap><section class='panel pad' style=max-width:920px>"
/* два блока: lr - русский, le - английский; лишний прячет стиль по языку */
"<div class=lr>"
"<h3>Справка по параметрам</h3>"
"<p>Наверху таблицы настроек &mdash; частота, порог, полярность и шкала; "
"остальное свёрнуто в блок «Дополнительно».</p>"
"<h4>Обнаружение события</h4><table>"
"<tr><th>Параметр</th><th>Что делает</th></tr>"
"<tr><td>Порог<br><code>threshold</code></td><td>На сколько кодов АЦП "
"отсчёт должен превысить базовую линию, чтобы считаться событием. "
"Ставится чуть выше шума &mdash; посмотрите шум на вкладке «Осциллограф».</td></tr>"
"<tr><td>Полярность<br><code>polarity</code></td><td>0 &mdash; импульсы "
"вверх от базовой линии, 1 &mdash; вниз (например, анод ФЭУ напрямую). "
"При 1 сигнал переворачивается ещё до обработки: порог и спектр "
"работают как с импульсами вверх, осциллограф "
"синхронизируется по фронту вниз. Постоянная составляющая сигнала "
"(например, база на &minus;4 В) на обработку не влияет &mdash; её "
"вычитает трапеция.</td></tr>"
"<tr><td>Гистерезис<br><code>hysteresis</code></td><td>Чтобы засчитать "
"следующее событие, сигнал должен сначала упасть ниже "
"доли порога. Задаётся В ПРОЦЕНТАХ: 50 значит, что трапеция должна "
"опуститься ниже половины порога. Прежний вариант вычитал единицу, "
"то есть гистерезиса практически не было.</td></tr>"
"<tr><td>Перезапуск<br><code>rearm</code></td><td>Сколько отсчётов жёстко "
"пропустить после пика, независимо от гистерезиса. Защита от дребезга "
"на спадающем хвосте. Это <b>не</b> метрика потерь, а параметр "
"перезапуска детектора.</td></tr>"
"</table>"

"<h4>Трапецеидальный фильтр</h4>"
"<p style=opacity:.7;font-size:13px>Алгоритм Джорданова-Нолла: разность "
"двух скользящих сумм, разнесённых на зазор. Даёт плоскую вершину &mdash; "
"амплитуда почти не зависит от того, в какую точку вершины попали. "
"Постоянная составляющая (базовая линия) вычитается автоматически: "
"обе суммы содержат её поровну.</p><table>"
"<tr><th>Параметр</th><th>Что делает</th></tr>"
"<tr><td>Окно L<br><code>trap_L</code></td><td>Длина окна "
"интегрирования: сколько отсчётов импульса суммируется. Определяет, "
"какая часть импульса идёт в амплитуду и насколько усредняется шум. "
"<b>Если окно задаётся парой RISE и FALL</b> (так бывает в других "
"приборах), окно интегрирования &mdash; их сумма: RISE=6, FALL=15 "
"соответствует нашему L&asymp;21.</td></tr>"
"<tr><td>Зазор G<br><code>trap_G</code></td><td>На сколько отсчётов "
"назад отстоит второе окно, которое вычитается. Оно должно целиком "
"лежать на базовой линии ДО импульса, поэтому G обязан быть больше "
"L плюс длительность фронта. Разность G&minus;L даёт длину плоской "
"вершины: чем она больше, тем устойчивее измеряется амплитуда.</td></tr>"
"<tr><td>Поиск пика<br><code>search</code></td><td>Сколько отсчётов после "
"срабатывания порога перебирать в поисках максимума трапеции. "
"Должно накрывать весь импульс.</td></tr>"
"</table>"

"<h4>Способ измерения амплитуды</h4>"
"<p style=opacity:.7;font-size:13px>Выбирается в меню «Способ измерения». "
"В таблице настроек параметры одного способа помечены <b>трапеция</b> "
"или <b>интегрирование</b>, без пометки &mdash; действуют всегда; "
"параметры, которые при выбранном способе не действуют, приглушены. Событие в обоих "
"способах обнаруживается одинаково &mdash; по трапеции, поэтому порог, "
"гистерезис, перезапуск, поиск пика, наложения, L и G нужны всегда (при "
"интегрировании по L и G ещё и находится вершина).</p><table>"
"<tr><th>Способ</th><th>Свои параметры</th></tr>"
"<tr><td>Трапеция</td><td>Амплитуда &mdash; вершина трапеции. Окно L и "
"зазор G задают и обнаружение, и измерение; дополнительно «Средн. по "
"плато». Базовая линия не нужна: трапеция вычитает её сама.</td></tr>"
"<tr><td>Интегрирование</td><td>Амплитуда &mdash; среднее отсчётов "
"импульса вокруг вершины за вычетом базы &mdash; 32 отсчётов перед фронтом этого же "
"импульса, как у свёртки. Свои параметры: «До вершины», «После вершины» (сколько "
"отсчётов брать, считаются прямо по осциллографу). «База 2^N» и «Окно базы» нужны, только "
"если перед импульсом нет этих 32 отсчётов (у самого начала потока). Масштаб у способов разный &mdash; после переключения подберите "
"«Кодов на канал». Смена способа очищает спектр.</td></tr>"
"<tr><td>Свёртка с эталоном</td><td>Амплитуда &mdash; высота, при которой эталонный "
"импульс фильтра формы лучше всего ложится на импульс (как в BecqMoni); база &mdash; 32 "
"отсчёта перед началом фронта (кончаются за L до пробоя порога), шкала &mdash; как у "
"интегрирования. Нужен эталон на этом окне «до / после вершины» и этой частоте, без "
"него амплитуда считается трапецией; новый эталон очищает спектр. На шумном ФЭУ этого "
"прибора после правки базы (2026-11-04) лучше интегрирования и трапеции (README).</td></tr>"
"</table>"

"<h4>Амплитуда и базовая линия</h4><table>"
"<tr><th>Параметр</th><th>Что делает</th></tr>"
"<tr><td>Кодов на канал<br><code>cpc</code></td><td>Сколько кодов "
"амплитуды приходится на один канал: 1 &mdash; канал равен коду АЦП, "
"0.5 &mdash; вдвое подробнее, 2 &mdash; вдвое грубее. <b>Верх шкалы = "
"каналов &times; кодов на канал</b>, он подписан рядом с полем. При "
"базе в середине АЦП запас вверх около 2047 кодов, поэтому 2048 "
"каналов по 1 коду покрывают его целиком. Амплитуда делится без "
"округления до кода, так что доли кода на канал дают настоящую "
"подробность, а не «гребёнку» пустых каналов. Изменение очищает "
"спектр.</td></tr>"
"<tr><td>Каналов<br><code>nch</code></td><td>Сколько каналов "
"показывать: 2048, 4096 или 8192. Это длина шкалы вправо; раскладка "
"событий от неё не зависит &mdash; спектр не сжимается и не "
"сбрасывается. Прибор копит все 8192 канала.</td></tr>"
"<tr><td>База 2^N<br><code>baseline_shift</code></td><td>Постоянная "
"времени фильтра, отслеживающего нулевой уровень между импульсами (бегущая "
"база: запасная для интегрирования и показ базы на странице). "
"Больше значение &mdash; медленнее и плавнее подстройка. Обычно "
"8&ndash;12, допустимо 1&ndash;16.</td></tr>"
"<tr><td>Окно базы<br><code>baseline_win</code></td><td>В среднее "
"берутся только отсчёты, отстоящие от текущей оценки нуля не дальше "
"этого значения. Нужно потому, что между импульсами сигнал редко "
"возвращается к настоящему нулю &mdash; мешают недоспавшие хвосты "
"(у нас tau&asymp;18 мкс). Без окна базовая линия уползала бы вверх "
"вслед за наложениями: при 10 000 имп/с ошибка была 190 кодов, "
"с окном стала 24. Ставить примерно вдвое-втрое больше размаха шума. "
"Если оценка долго не находит похожих отсчётов, ноль перезахватывается "
"автоматически.</td></tr>"
"<tr><td>Средн. по плато<br><code>flat_avg</code></td><td>Брать "
"амплитуду как среднее по плоской вершине трапеции вместо максимума. "
"<b>По умолчанию выключено.</b> Проверено симуляцией: усреднение "
"выигрывает только при длинном плато (G&minus;L от 19 отсчётов). "
"При коротком плато максимум даёт даже меньший разброс &mdash; "
"соседние отсчёты трапеции сильно скоррелированы, усреднять почти "
"нечего. При (G&minus;L) меньше 16 происходит автоматический откат "
"на максимум. Смещение максимума растёт с шумом, но это постоянный "
"сдвиг для всех амплитуд &mdash; уходит в калибровку шкалы и ширину "
"пиков не портит.</td></tr>"
"</table>"
"<h4>Отбраковка наложений</h4>"
"<p style=opacity:.7;font-size:13px>Меньше проценты &mdash; строже "
"отбраковка, больше событий уходит в брак. Следите за счётчиком "
"«наложений» в статистике.</p><table>"
"<tr><th>Параметр</th><th>Что делает</th></tr>"
"<tr><td>Наложение до %<br><code>pileup_pre_pct</code></td><td>Если "
"<b>перед</b> пиком трапеция уже выше этого процента от амплитуды &mdash; "
"импульс сидит на хвосте предыдущего, бракуем.</td></tr>"
"<tr><td>Наложение после %<br><code>pileup_post_pct</code></td><td>Если "
"<b>после</b> пика трапеция не упала ниже этого процента &mdash; "
"на импульс наложился следующий, бракуем.</td></tr>"
"</table>"

"<h4>Режимы</h4><table>"
"<tr><th>Режим</th><th>Назначение</th></tr>"
"<tr><td>Спектр</td><td>Набор гистограммы. Поля «пик от / до» выделяют "
"участок спектра: для него считаются центр, ширина на полувысоте и "
"разрешение. Пик удобнее выделять мышью прямо на графике: <b>щелчок</b> по пику "
"&mdash; участок вокруг него (центр ±1.5 FWHM), <b>протяжка</b> &mdash; свой участок, "
"<b>двойной щелчок</b> &mdash; сброс; итог подписывается над пиком, а под курсором "
"видны канал, энергия и счёт. Всё про калибровку &mdash; под кнопкой «Калибровка» (снаружи "
"только сводка в строку). Блок «Калибровка по энергии»: выберите изотоп и "
"«Откалибровать» &mdash; страница найдёт линии, подгонит пики и построит шкалу E(канал); "
"«Сохранить в настройки» записывает её в прибор (до этого она только на странице и "
"помечена «не сохранена»). «Показать линии калибровки» отмечает линии изотопа на "
"графике; под кнопкой же &mdash; шкала, таблица линий и поля c0&ndash;c4: коэффициенты "
"можно вписать вручную (E = c0 + c1&middot;k + &hellip; + c4&middot;k<sup>4</sup>, кэВ) и нажать "
"«Применить». Там же калибровка <b>по точкам</b> (до 4): при открытой «Калибровке» щелчок "
"по пику на спектре (или протяжка по нему) подгоняет пик и ставит его центр в строку, "
"выбранную кнопкой номера, или в первую свободную; впишите энергию линии и нажмите "
"«Построить по точкам» (1 точка &mdash; прямая через ноль, 2&ndash;3 &mdash; прямая, 4 &mdash; "
"парабола). <b>Мощность дозы</b> считается по спектру, когда в приборе сохранена калибровка "
"для текущих настроек: каждое событие добавляет вес G(E) своей энергии, функция G подогнана по "
"модели кристалла выбранного блока &mdash; NaI(Tl) Ø25×25, 30×40, 30×70, 40×40, 63×63 или 80×80 "
"(Монте-Карло, H*(10) по ICRP 74 и керма в воздухе); у каждого блока своя поправка k. "
"В строке статуса &mdash; мощность дозы в мкЗв/ч за последние 10 с набора, обновляется раз в "
"секунду (в подсказке &mdash; окно, погрешность и мкР/ч), в блоке «Мощность "
"дозы» &mdash; средняя с «Сброса». Ошибка модели от 50 кэВ до 3 МэВ &mdash; в среднем 3 %, "
"до 14&ndash;17 %, если источник сбоку; в торец детектора мягкое излучение занижается до "
"35 %. Поправку k можно подогнать по дозиметру: положите его рядом, «Сброс», подождите "
"несколько минут, впишите показание и «Подогнать k». Или <b>пробой KCl</b>: пустой домик &mdash; "
"«Сброс», набор, «Запомнить фон» (спектр хранит браузер); банка с KCl под торцом детектора "
"&mdash; «Сброс», набор столько же, впишите массу, диаметр банки, высоту слоя и зазор от соли "
"до кристалла, «Посчитать по пробе»: страница подгонит чистый пик 1461 кэВ, посчитает его "
"по модели кристалла (Монте-Карло прямо в браузере) и предложит k = модель / измерено "
"(«Записать k»). Космические мюоны (за шкалой) в дозу не "
"входят, поэтому на фоне прибор показывает меньше дозиметра. Шкала годится, пока не сменились способ измерения, окна, L, G, «кодов на канал», "
"полярность и частота; иначе блок скажет, что калибровку пора набрать заново. "
"С калибровкой измеритель пика показывает энергию и разрешение <b>от энергии "
"линии</b> &mdash; процент по каналам зависит от нуля шкалы, и сравнивать по нему "
"способы нечестно; ось X переключается «каналы / кэВ» над графиком, линии изотопа "
"на графике &mdash; использованные для калибровки ярко, остальные бледно, а файлы "
"выгружаются с калибровкой. Кнопки «выгрузить» сохраняют спектр файлом: "
"XML (ResultDataFile, открывается в BecqMoni), CSV (канал и счёт), "
"N42 (ANSI N42.42) и SPE (SpectraLine). "
"Выгружаются каналы, видимые на экране; время замера берётся из часов "
"компьютера &mdash; своих часов у прибора нет. Ниже &mdash; мониторинг CPS: история за "
"6 часов по секунде хранится в приборе и копится, пока идёт набор, даже при "
"закрытой странице; график складывает её в интервалы «усреднение, с» и рисует "
"скользящее среднее. Шаг 0.1, 0.25 и 0.5 с берётся из быстрой истории прибора: "
"по 0.1 с за последние 10 минут (шаг 0.25 с &mdash; по 2&ndash;3 таких отсчёта, CPS по "
"их настоящему времени). Видно окно по времени (по умолчанию последние 30 с, выбор "
"«окно»), оно едет за новыми данными; колесо &mdash; масштаб, перетаскивание "
"&mdash; сдвиг в прошлое (там окно стоит на месте), двойной щелчок &mdash; обратно "
"к окну и текущему моменту. CPS интервала &mdash; импульсы, делённые "
"на время, пока реально шёл набор; &delta; &mdash; статистическая погрешность "
"1/&radic;N. Время прибора берётся по SNTP или от браузера.</td></tr>"
"<tr><td>Осциллограф</td><td>Сырые отсчёты с синхронизацией "
"по фронту: как только сигнал вырос за 8 отсчётов не меньше чем на "
"«синхр. по фронту», момент срабатывания ставится на пятую часть "
"экрана (синяя метка), и импульс стоит на месте. Переключатель "
"<b>Авто</b> / <b>Ждущий</b>: в авто без фронта 0.15 с развёртка идёт "
"свободно, чтобы были видны база и шум; ждущий обновляется только по "
"фронту и держит последний пойманный импульс. Ширину окна задаёт "
"«развёртка» &mdash; от 64 до 32768 отсчётов (при 8 МГц это 4 мс), "
"цена деления подписана на графике. «Амплитуда от&ndash;до»: "
"синхронизация по амплитуде &mdash; кадр показывается, только если высота "
"импульса над базой в этом диапазоне кодов АЦП (0 и 0 &mdash; без фильтра); "
"высота показанного импульса и сколько импульсов отброшено &mdash; под "
"графиком. «Ось X»: подписи сетки в "
"микросекундах или в отсчётах. Отсчёты считаются от момента "
"синхронизации (синяя метка = 0, до неё &mdash; отрицательные), так "
"что числа для L, G, «до/после вершины», перезапуска и поиска пика "
"читаются прямо с экрана. <b>Линейка</b>: щелчок по графику ставит "
"метки A (начало), B (вершина &mdash; прилипает к пику рядом со "
"щелчком) и C (конец), метки можно перетаскивать. Под графиком "
"&mdash; расстояния A&rarr;B, B&rarr;C, A&rarr;C в отсчётах и "
"микросекундах и высота вершины; кнопка «задать до / после вершины» "
"сразу записывает B&minus;A и C&minus;B в параметры интегрирования. Трапеция "
"на экран не выводится &mdash; она считается в приборе, а строка "
"«шум фильтра до импульса» показывает, выше ли порог её шума. Спектр в этом "
"режиме не набирается. «Ось Y от»: от базовой линии, от середины шкалы "
"(2048) или в настоящих кодах АЦП. «Вся шкала» показывает весь диапазон "
"АЦП, а строка «запас по шкале» &mdash; сколько кодов осталось от базы "
"до потолка и до пола.</td></tr>"
"<tr><td>Настройки</td><td>Отдельная страница (ссылка «Настройки» вверху). "
"<b>Сеть</b> &mdash; адрес прибора: от роутера (DHCP) или статический. <b>Прошивка</b> &mdash; "
"версия и обновление по сети файлом build/esp32_mca.bin. <b>Настройки АЦП</b> &mdash; "
"частота, порог, полярность, шкала спектра и в блоке «Дополнительно» все "
"параметры обработки; в прибор уходят кнопкой «Применить». Смена «Кодов на "
"канал» и способа измерения очищает спектр, смена L, G и полярности "
"перезапускает фильтр.</td></tr>"
"<tr><td>Фильтр формы</td><td>На странице «Настройки», по желанию. Отбрасывает импульсы, ширина "
"которых (площадь / высота в окне «до / после вершины», база &mdash; перед фронтом) дальше от "
"эталона, чем допуск: шумовые всплески ФЭУ уже сцинтилляций. Узкий допуск (меньше ~20 %) режет "
"и настоящие малые импульсы. «Набрать эталон» &mdash; при запущенном наборе (хватит и фона), "
"прибор берёт 2000 импульсов. Эталон годится для того окна и той частоты, на которых набран. "
"Графики: средний эталонный импульс с последними импульсами в допуске и вне его и гистограмма "
"ширины, полоса &mdash; эталон ± допуск. На 20 МГц при ~5000 имп/с потерь чанков нет, но запас ядра "
"небольшой. С хорошим ФЭУ "
"фильтр не нужен.</td></tr>"
"<tr><td>Диагностика</td><td>Отдельная страница. <b>Лог прибора</b> &mdash; сообщения "
"прошивки с начала работы (последние 256 строк), цвет по уровню: красные &mdash; "
"ошибки, жёлтые &mdash; предупреждения, тег окрашен по подсистеме (АЦП, сеть, прошивка...); "
"галочки уровней и подсистем скрывают лишнее, «Сохранить» пишет лог в файл. Лог виден и тогда, когда консоль молчит из-за эмуляции MCA на "
"COM. <b>Диагностика АЦП</b> &mdash; поток отсчётов и его реальная скорость, регистры "
"камерного интерфейса, профиль за последнюю секунду (загрузка обработки &mdash; та же, что в "
"строке состояния спектра, загрузка ядра 0, такты на отсчёт, запросы, память), журнал потерь чанков, побитовая статистика линий данных "
"и их проверка.</td></tr>"
"</table></div>"

"<div class=le>"
"<h3>Parameter reference</h3>"
"<p>The top of the settings table holds the sample rate, threshold, polarity and "
"scale; the rest is folded into the “Advanced” block.</p>"
"<h4>Event detection</h4><table>"
"<tr><th>Parameter</th><th>What it does</th></tr>"
"<tr><td>Threshold<br><code>threshold</code></td><td>How many ADC codes a sample "
"must exceed the baseline by to count as an event. Set just above the noise &mdash; "
"look at the noise on the “Oscilloscope” tab.</td></tr>"
"<tr><td>Polarity<br><code>polarity</code></td><td>0 &mdash; pulses go up from the "
"baseline, 1 &mdash; down (for example, PMT anode directly). With 1 the signal is "
"inverted before processing: the threshold and spectrum work as with upward pulses, "
"the oscilloscope triggers on a falling edge. A DC offset of the signal (for example, "
"a baseline at &minus;4 V) does not affect processing &mdash; the trapezoid subtracts "
"it.</td></tr>"
"<tr><td>Hysteresis<br><code>hysteresis</code></td><td>To count the next event, the "
"signal must first drop below a fraction of the threshold. Set IN PERCENT: 50 means "
"the trapezoid must drop below half the threshold. The previous version subtracted "
"one, so there was practically no hysteresis.</td></tr>"
"<tr><td>Re-arm<br><code>rearm</code></td><td>How many samples to skip "
"unconditionally after the peak, regardless of hysteresis. Protection against "
"chatter on the falling tail. This is <b>not</b> a loss metric but a detector "
"re-arm parameter.</td></tr>"
"</table>"

"<h4>Trapezoidal filter</h4>"
"<p style=opacity:.7;font-size:13px>Jordanov&ndash;Knoll algorithm: the difference "
"of two moving sums separated by a gap. It gives a flat top &mdash; the amplitude "
"hardly depends on which point of the top is hit. The DC component (baseline) is "
"subtracted automatically: both sums contain it equally.</p><table>"
"<tr><th>Parameter</th><th>What it does</th></tr>"
"<tr><td>Window L<br><code>trap_L</code></td><td>Integration window length: how "
"many pulse samples are summed. It determines which part of the pulse goes into the "
"amplitude and how much the noise is averaged. <b>If the window is set by a RISE and "
"FALL pair</b> (as in some other devices), the integration window is their sum: "
"RISE=6, FALL=15 corresponds to our L&asymp;21.</td></tr>"
"<tr><td>Gap G<br><code>trap_G</code></td><td>How many samples back the second, "
"subtracted window is. It must lie entirely on the baseline BEFORE the pulse, so G "
"must be greater than L plus the rise time. The difference G&minus;L is the flat-top "
"length: the longer it is, the more stable the amplitude measurement.</td></tr>"
"<tr><td>Peak search<br><code>search</code></td><td>How many samples after the "
"threshold crossing to search for the trapezoid maximum. Must cover the whole "
"pulse.</td></tr>"
"</table>"

"<h4>Amplitude method</h4>"
"<p style=opacity:.7;font-size:13px>Selected in the “Method” menu. In the settings "
"table the parameters of one method are marked <b>trapezoid</b> or "
"<b>integration</b>, unmarked ones always apply; parameters that have no effect with "
"the selected method are dimmed. Events are detected the same way with both methods "
"&mdash; by the trapezoid, so threshold, hysteresis, re-arm, peak search, pile-up, L "
"and G are always needed (with integration L and G also locate the peak).</p><table>"
"<tr><th>Method</th><th>Its own parameters</th></tr>"
"<tr><td>Trapezoid</td><td>Amplitude &mdash; the top of the trapezoid. Window L and "
"gap G set both detection and measurement; in addition “Flat-top avg”. No baseline "
"is needed: the trapezoid subtracts it itself.</td></tr>"
"<tr><td>Integration</td><td>Amplitude &mdash; the mean of the pulse samples around "
"the peak minus the baseline &mdash; 32 samples before the rise of the same pulse, as in "
"convolution. Its own parameters: “Before peak”, “After peak” (how many samples to take, "
"counted straight from the oscilloscope). “Baseline 2^N” and “Baseline window” are needed "
"only if those 32 samples are not there before the pulse (at the very start of the "
"stream). The methods have different scales &mdash; after switching, "
"adjust “Codes per channel”. Changing the method clears the spectrum.</td></tr>"
"<tr><td>Convolution with the reference</td><td>Amplitude &mdash; the height at which the "
"shape-filter reference pulse fits the pulse best (as in BecqMoni); the baseline is 32 samples "
"before the rise starts (ending L before the threshold crossing), the scale is that of "
"integration. It needs a reference for this “before / after peak” window and this "
"rate, without one the amplitude is computed by the trapezoid; a new reference clears the "
"spectrum. On the noisy PMT of this device, after the baseline fix (2026-11-04), it is "
"better than integration and the trapezoid (README).</td></tr>"
"</table>"

"<h4>Amplitude and baseline</h4><table>"
"<tr><th>Parameter</th><th>What it does</th></tr>"
"<tr><td>Codes per channel<br><code>cpc</code></td><td>How many amplitude codes per "
"channel: 1 &mdash; a channel equals an ADC code, 0.5 &mdash; twice as fine, 2 "
"&mdash; twice as coarse. <b>Top of scale = channels &times; codes per channel</b>, "
"shown next to the field. With the baseline in the middle of the ADC the headroom is "
"about 2047 codes, so 2048 channels of 1 code cover it entirely. The amplitude is "
"divided without rounding to a code, so fractions of a code per channel give real "
"detail rather than a “comb” of empty channels. Changing it clears the "
"spectrum.</td></tr>"
"<tr><td>Channels<br><code>nch</code></td><td>How many channels to show: 2048, 4096 "
"or 8192. This is the scale length to the right; how events are laid out does not "
"depend on it &mdash; the spectrum is neither compressed nor reset. The device "
"accumulates all 8192 channels.</td></tr>"
"<tr><td>Baseline 2^N<br><code>baseline_shift</code></td><td>Time constant of the "
"filter that tracks the zero level between pulses (running baseline: a fallback for "
"integration and the baseline shown on the page). Larger value &mdash; slower and "
"smoother tracking. Usually 8&ndash;12, allowed 1&ndash;16.</td></tr>"
"<tr><td>Baseline window<br><code>baseline_win</code></td><td>Only samples within "
"this distance from the current zero estimate are averaged. This is needed because "
"between pulses the signal rarely returns to the true zero &mdash; unfinished tails "
"get in the way (ours have tau&asymp;18 µs). Without the window the baseline would "
"creep up following pile-ups: at 10,000 cps the error was 190 codes, with the window "
"it became 24. Set about two to three times the noise swing. If the estimate finds "
"no similar samples for a long time, the zero is re-captured automatically.</td></tr>"
"<tr><td>Flat-top avg<br><code>flat_avg</code></td><td>Take the amplitude as the "
"mean over the flat top of the trapezoid instead of the maximum. <b>Off by "
"default.</b> Verified by simulation: averaging wins only with a long flat top "
"(G&minus;L from 19 samples). With a short flat top the maximum gives even less "
"spread &mdash; neighbouring trapezoid samples are strongly correlated, there is "
"little to average. With (G&minus;L) below 16 it automatically falls back to the "
"maximum. The bias of the maximum grows with noise, but it is a constant shift for "
"all amplitudes &mdash; it goes into the scale calibration and does not widen the "
"peaks.</td></tr>"
"</table>"
"<h4>Pile-up rejection</h4>"
"<p style=opacity:.7;font-size:13px>Lower percentages &mdash; stricter rejection, "
"more events are rejected. Watch the pile-up counter in the statistics.</p><table>"
"<tr><th>Parameter</th><th>What it does</th></tr>"
"<tr><td>Pile-up before %<br><code>pileup_pre_pct</code></td><td>If <b>before</b> "
"the peak the trapezoid is already above this percentage of the amplitude &mdash; "
"the pulse sits on the tail of the previous one, reject.</td></tr>"
"<tr><td>Pile-up after %<br><code>pileup_post_pct</code></td><td>If <b>after</b> "
"the peak the trapezoid has not dropped below this percentage &mdash; the next pulse "
"piled up on this one, reject.</td></tr>"
"</table>"

"<h4>Modes</h4><table>"
"<tr><th>Mode</th><th>Purpose</th></tr>"
"<tr><td>Spectrum</td><td>Histogram acquisition. The “peak from / to” fields select "
"a part of the spectrum: the centroid, FWHM and resolution are computed for it. A peak is "
"easier to select with the mouse right on the plot: a <b>click</b> on a peak &mdash; the "
"range around it (centroid ±1.5 FWHM), a <b>drag</b> &mdash; your own range, a <b>double "
"click</b> &mdash; reset; the result is labelled above the peak, and the channel, energy and "
"count are shown under the cursor. Everything about calibration is under the “Calibration” "
"button (outside - a one-line summary). The “Energy calibration” block: choose the isotope and "
"“Calibrate” &mdash; the page finds the lines, fits the peaks and builds the E(channel) scale; "
"“Save to settings” writes it to the device (until then it is only on the page and marked "
"“not saved”). “Show calibration lines” marks the isotope lines on the plot; under the same "
"button are the scale, the table of lines and the c0&ndash;c4 fields: the coefficients can be "
"entered by hand (E = c0 + c1&middot;k + &hellip; + c4&middot;k<sup>4</sup>, keV) with "
"“Apply”. Calibration <b>by points</b> (up to 4) is there too: with “Calibration” open, a click "
"on a peak in the spectrum (or a drag over it) fits the peak and puts its centroid into the row "
"chosen with its number button or into the first free one; enter the line energy and press "
"“Build from points” (1 point &mdash; a line through zero, 2&ndash;3 &mdash; a line, 4 &mdash; "
"a parabola). The <b>dose rate</b> is computed from the spectrum when the device holds a "
"calibration for the current settings: each event adds the weight G(E) of its energy, the G "
"function is fitted on a model of the crystal of the chosen block &mdash; NaI(Tl) Ø25×25, 30×40, "
"30×70, 40×40, 63×63 or 80×80 (Monte Carlo, H*(10) by ICRP 74 and air kerma); each block has its "
"own correction k. The status line shows the dose rate in µSv/h over the last 10 s of "
"acquisition, updated every second (the tooltip has the window, error and µR/h), the “Dose "
"rate” block the mean since “Reset”. The model error from 50 keV to 3 MeV is 3 % on average, "
"up to 14&ndash;17 % with the source at the side; at the detector end face soft radiation is "
"underestimated by up to 35 %. The correction k can be fitted by a dosimeter: put it next to "
"the detector, “Reset”, wait a few minutes, enter its reading and “Fit k”. Or with a <b>KCl "
"sample</b>: empty castle &mdash; “Reset”, acquire, “Store background” (the browser keeps the "
"spectrum); a KCl jar under the detector end face &mdash; “Reset”, acquire as long, enter the mass, "
"jar diameter, layer height and the gap from the salt to the crystal, “Compute from sample”: "
"the page fits the net 1461 keV peak, computes it by the crystal model (Monte Carlo right in "
"the browser) and offers k = model / measured (“Save k”). Cosmic muons "
"(off scale) are not in the dose, so at background the device shows less than a dosimeter. "
"The scale is valid until the measurement method, windows, L, G, “codes per channel”, "
"polarity or rate change; otherwise the block says the calibration must be made again. "
"With a calibration the peak meter shows the energy and the resolution <b>from the line "
"energy</b> &mdash; the percentage by channels depends on the scale zero, and comparing "
"methods by it is unfair; the X axis switches “channels / keV” above the plot, the isotope "
"lines on the plot &mdash; the ones used for the calibration bright, the others faint, and "
"files are exported with the calibration. The “export” buttons save the spectrum to a file: XML (ResultDataFile, opens in "
"BecqMoni), CSV (channel and count), N42 (ANSI N42.42) and SPE (SpectraLine). The "
"channels visible on screen are exported; the measurement time is taken from the "
"computer clock &mdash; the device has no clock of its own. Below is the CPS "
"monitor: a 6-hour history at one sample per second is kept in the device and "
"accumulates while the spectrum is acquired, even with the page closed; the "
"plot sums it into “averaging, s” intervals and draws a moving average. A step "
"of 0.1, 0.25 and 0.5 s comes from the fast history of the device: 0.1 s samples "
"over the last 10 minutes (a 0.25 s step takes 2&ndash;3 such samples, CPS by their "
"real time). It shows a time window (the last 30 s by default, the “window” selector) that "
"follows new data; wheel &mdash; zoom, drag &mdash; move into the past (the "
"window then stays put), double click &mdash; back to the window and now. The CPS "
"of an interval is the counts divided by the time acquisition actually ran; "
"&delta; is the statistical error 1/&radic;N. The device time comes from SNTP "
"or from the browser.</td></tr>"
"<tr><td>Oscilloscope</td><td>Raw samples with "
"edge triggering: as soon as the signal rises by at least “trigger on edge” within "
"8 samples, the trigger point is placed at one fifth of the screen (blue mark), and "
"the pulse stays in place. The <b>Auto</b> / <b>Normal</b> switch: in auto, with no "
"edge for 0.15 s the sweep runs freely so the baseline and noise are visible; normal "
"refreshes only on an edge and holds the last captured pulse. The window width is "
"set by “timebase” &mdash; from 64 to 32768 samples (at 8 MHz that is 4 ms), the "
"time per division is shown on the plot. “Amplitude from&ndash;to”: amplitude "
"triggering &mdash; a frame is shown only if the pulse height above the baseline is "
"within this range of ADC codes (0 and 0 &mdash; no filter); the height of the shown "
"pulse and how many pulses were rejected are shown under the plot. “X axis”: grid "
"labels in microseconds or in samples. Samples are counted from the trigger point "
"(blue mark = 0, negative before it), so the numbers for L, G, “before/after peak”, "
"re-arm and peak search are read straight from the screen. <b>Ruler</b>: a click on "
"the plot places marks A (start), B (peak &mdash; snaps to the maximum near the "
"click) and C (end); marks can be dragged. Under the plot &mdash; the distances "
"A&rarr;B, B&rarr;C, A&rarr;C in samples and microseconds and the height of the "
"peak; the “set before / after peak” button writes B&minus;A and C&minus;B into the "
"integration parameters right away. The trapezoid is not drawn &mdash; it is computed in the "
"device, and the “filter noise before the pulse” line shows whether the threshold is "
"above its noise. The spectrum is not acquired in this mode. “Y axis from”: from the baseline, from the "
"middle of the scale (2048) or in true ADC codes. “Full scale” shows the whole ADC "
"range, and the “headroom from baseline” line shows how many codes are left from the "
"baseline to the ceiling and the floor.</td></tr>"
"<tr><td>Settings</td><td>A separate page (the “Settings” link at the top). "
"<b>Network</b> &mdash; the device address: from the router (DHCP) or static. <b>Firmware</b> "
"&mdash; the version and an update over the network with the build/esp32_mca.bin file. "
"<b>ADC settings</b> &mdash; sample rate, threshold, polarity, spectrum scale and, in "
"the “Advanced” block, all processing parameters; they go to the device with the "
"“Apply” button. Changing “Codes per channel” or the method clears the spectrum, "
"changing L, G or polarity restarts the filter.</td></tr>"
"<tr><td>Shape filter</td><td>On the “Settings” page, on demand. Rejects pulses whose width "
"(area / height in the “before / after peak” window, baseline before the rise) is farther from "
"the reference than the tolerance: PMT noise bursts are narrower than scintillations. A narrow "
"tolerance (below ~20 %) also cuts real small pulses. “Learn reference” &mdash; with a "
"running acquisition (background is enough), the device takes 2000 pulses. The reference is valid for the "
"window and rate it was learned at. Plots: the mean reference pulse with the last pulses within "
"and outside the tolerance, and a width histogram, the band is reference ± tolerance. At 20 MHz "
"and ~5000 cps there are no chunk losses, but the core headroom is small. With a good PMT the filter "
"is not needed.</td></tr>"
"<tr><td>Diagnostics</td><td>A separate page. <b>Device log</b> &mdash; firmware "
"messages since start (the last 256 lines), coloured by level: red &mdash; errors, "
"yellow &mdash; warnings, the tag is coloured by subsystem (ADC, network, firmware...); "
"the level and subsystem checkboxes hide what is not needed, “Save” writes the log to a file. The log is visible even when the console is silent because of MCA emulation "
"on COM. <b>ADC diagnostics</b> &mdash; the sample stream and its real rate, camera "
"interface registers, the last-second profile (processing load &mdash; the same as in the spectrum "
"status line, core 0 load, cycles per "
"sample, requests, memory), the chunk loss log, per-bit statistics of the data lines "
"and their check.</td></tr>"
"</table></div>"

"</section></div></body></html>";

static esp_err_t h_help(httpd_req_t *r)
{
    httpd_resp_set_type(r, "text/html; charset=utf-8");
    return httpd_resp_send(r, HELP_PAGE, HTTPD_RESP_USE_STRLEN);
}


/* ---- ВЫГРУЗКА СПЕКТРА ----
 * Форматы и разметка - общепринятые у программ для гамма-спектров,
 * чтобы файлы открывались ими без переделки: XML - ResultDataFile
 * (BecqMoni), CSV - заголовок и строки «канал,счёт», N42 - ANSI
 * N42.42-2011, SPE - ЛСРМ
 * SpectraLine (текстовый заголовок и счёты uint32 little-endian).
 * Калибровка по энергии - из запроса: c0, c1 и, если есть, c2..c4 (E =
 * c0 + c1*канал + ... + c4*канал^4, кэВ). Её присылает страница, если калибровка
 * годится для нынешних настроек; без неё блоки калибровки пропускаются.
 * Выгружается то, что видно на экране:
 * первые nch каналов.
 * Часов реального времени у прибора нет, поэтому время замера
 * присылает страница: t - Unix-время в секундах, tz - смещение пояса
 * в минутах. Без них время помечается как неизвестное. */
typedef struct {
    uint32_t *d;          /* снимок каналов 0..n-1                     */
    size_t    n;
    uint64_t  sum;        /* событий в выгружаемых каналах             */
    uint64_t  total;      /* всего найдено импульсов, с отбракованными */
    double    treal, tlive;
    struct tm ts, te;     /* начало и конец замера, местное время      */
    bool      tvalid;
    /* httpd_resp_set_hdr запоминает указатель, а не копию: строка
     * обязана жить до конца отправки, поэтому она здесь, а не на стеке
     * вспомогательной функции */
    char      disp[96];
    double    cal[5];         /* калибровка E(канал), от младшей степени   */
    int       ncal;           /* сколько коэффициентов (0 - нет, 2..5)     */
} exp_snap_t;

#define TMA(t) (t)->tm_year + 1900, (t)->tm_mon + 1, (t)->tm_mday, \
               (t)->tm_hour, (t)->tm_min, (t)->tm_sec

static bool exp_snap(httpd_req_t *r, exp_snap_t *s)
{
    mca_params_t p;
    mca_stats_t  st;
    mca_dsp_get_params(&p);
    mca_dsp_get_stats(&st);

    /* снимок целиком, иначе сумма в заголовке разошлась бы с каналами */
    s->n = (size_t)p.nch > MCA_CHANNELS ? MCA_CHANNELS : (size_t)p.nch;
    s->d = heap_caps_malloc(s->n * sizeof(uint32_t), MALLOC_CAP_SPIRAM);
    if (!s->d) s->d = malloc(s->n * sizeof(uint32_t));
    if (!s->d) return false;
    for (size_t off = 0; off < s->n; off += 512)
        mca_dsp_get_spectrum(s->d + off, off,
                             s->n - off < 512 ? s->n - off : 512);
    s->sum = 0;
    for (size_t i = 0; i < s->n; i++) s->sum += s->d[i];
    s->total = st.total_events + st.skipped_pileup;

    /* живое время - за вычетом мёртвого, та же доля, что в статусе */
    s->treal = st.run_ms / 1000.0;
    double dead = st.samples_processed
                ? (double)st.skipped_deadtime / (double)st.samples_processed
                : 0.0;
    s->tlive = s->treal * (1.0 - dead);

    char q[256], v[32];
    long long t = 0;
    int tz = 0;
    s->ncal = 0;
    if (httpd_req_get_url_query_str(r, q, sizeof(q)) == ESP_OK) {
        if (httpd_query_key_value(q, "t", v, sizeof(v)) == ESP_OK)
            t = atoll(v);
        if (httpd_query_key_value(q, "tz", v, sizeof(v)) == ESP_OK)
            tz = atoi(v);
        static const char *ck[5] = { "c0", "c1", "c2", "c3", "c4" };
        for (int k = 0; k < 5; k++) {
            if (httpd_query_key_value(q, ck[k], v, sizeof(v)) != ESP_OK) break;
            char *e;
            const double c = strtod(v, &e);
            if (e == v || !isfinite(c)) break;
            s->cal[k] = c;
            s->ncal = k + 1;
        }
        if (s->ncal < 2 || !(s->cal[1] > 0)) s->ncal = 0;
    }
    /* страница не прислала время - берём часы прибора (UTC), если заданы */
    if (t <= 1500000000LL && mca_time_now_ms()) {
        t  = mca_time_now_ms() / 1000;
        tz = 0;
    }
    s->tvalid = t > 1500000000LL;
    /* местное время считаем как UTC со сдвигом пояса - так не нужна
     * настройка часового пояса на самом приборе */
    time_t te = (time_t)(t + (long long)tz * 60);
    time_t ts = te - (time_t)(s->treal + 0.5);
    gmtime_r(&te, &s->te);
    gmtime_r(&ts, &s->ts);
    return true;
}

static void exp_head(httpd_req_t *r, exp_snap_t *s, const char *ext,
                     const char *type)
{
    if (s->tvalid)
        snprintf(s->disp, sizeof(s->disp),
                 "attachment; filename=\"mca_%04d%02d%02d_%02d%02d%02d.%s\"",
                 TMA(&s->te), ext);
    else
        snprintf(s->disp, sizeof(s->disp),
                 "attachment; filename=\"mca_spectrum.%s\"", ext);
    httpd_resp_set_type(r, type);
    httpd_resp_set_hdr(r, "Content-Disposition", s->disp);
}

static void exp_xml(httpd_req_t *r, exp_snap_t *s, char *b)
{
    exp_head(r, s, "xml", "application/xml");
    /* калибровка - как пишет её BecqMoni: полином от младшей степени */
    char cal[700] = "";
    if (s->ncal) {
        int p = snprintf(cal, sizeof(cal),
            "        <EnergyCalibration>\r\n"
            "          <PolynomialOrder>%d</PolynomialOrder>\r\n"
            "          <Coefficients>\r\n", s->ncal - 1);
        for (int k = 0; k < s->ncal; k++)
            p += snprintf(cal + p, sizeof(cal) - p,
                "            <Coefficient>%.9g</Coefficient>\r\n", s->cal[k]);
        snprintf(cal + p, sizeof(cal) - p,
            "          </Coefficients>\r\n"
            "        </EnergyCalibration>\r\n");
    }
    httpd_resp_sendstr_chunk(r,
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
        "<ResultDataFile xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\""
        " xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">\r\n"
        "  <FormatVersion>120920</FormatVersion>\r\n"
        "  <ResultDataList>\r\n"
        "    <ResultData>\r\n");
    if (!s->tvalid)
        httpd_resp_sendstr_chunk(r,
            "      <!-- TIME UNKNOWN: the device has no clock -->\r\n");
    int n = snprintf(b, 4096,
        "      <SampleInfo>\r\n"
        "        <Name>MCA AD9226</Name>\r\n"
        "        <Location />\r\n"
        "        <Time>%04d-%02d-%02dT%02d:%02d:%02d</Time>\r\n"
        "        <Weight>1</Weight>\r\n"
        "        <Volume>1</Volume>\r\n"
        "        <Note />\r\n"
        "      </SampleInfo>\r\n"
        "      <DeviceConfigReference>\r\n"
        "        <Name>MCA AD9226</Name>\r\n"
        "        <Guid>00000000-0000-0000-0000-000000000000</Guid>\r\n"
        "      </DeviceConfigReference>\r\n"
        "      <StartTime>%04d-%02d-%02dT%02d:%02d:%02d</StartTime>\r\n"
        "      <EndTime>%04d-%02d-%02dT%02d:%02d:%02d</EndTime>\r\n"
        "      <PresetTime>0</PresetTime>\r\n"
        "      <EnergySpectrum>\r\n"
        "        <NumberOfChannels>%u</NumberOfChannels>\r\n"
        "        <ChannelPitch>1</ChannelPitch>\r\n"
        "%s"
        "        <ValidPulseCount>%" PRIu64 "</ValidPulseCount>\r\n"
        "        <TotalPulseCount>%" PRIu64 "</TotalPulseCount>\r\n"
        "        <MeasurementTime>%lu</MeasurementTime>\r\n"
        "        <LiveTime>%.1f</LiveTime>\r\n"
        "        <NumberOfSamples>1</NumberOfSamples>\r\n"
        "        <Spectrum>\r\n",
        TMA(&s->ts), TMA(&s->ts), TMA(&s->te), (unsigned)s->n, cal,
        s->sum, s->total, (unsigned long)(s->treal + 0.5), s->tlive);
    httpd_resp_send_chunk(r, b, n);
    for (size_t i = 0; i < s->n; ) {
        int pos = 0;
        for (int j = 0; j < 80 && i < s->n; j++, i++)
            pos += snprintf(b + pos, 4096 - pos,
                            "          <DataPoint>%" PRIu32 "</DataPoint>\r\n",
                            s->d[i]);
        httpd_resp_send_chunk(r, b, pos);
    }
    httpd_resp_sendstr_chunk(r,
        "        </Spectrum>\r\n"
        "      </EnergySpectrum>\r\n"
        "      <PulseCollection>\r\n"
        "        <Format>Base64 encoded binary</Format>\r\n"
        "        <Pulses />\r\n"
        "      </PulseCollection>\r\n"
        "    </ResultData>\r\n"
        "  </ResultDataList>\r\n"
        "</ResultDataFile>\r\n");
}

static void exp_csv(httpd_req_t *r, exp_snap_t *s, char *b)
{
    /* простой CSV: одна строка-заголовок, дальше
     * «канал,счёт» с канала 0, без пробелов, окончания CRLF */
    exp_head(r, s, "csv", "text/csv");
    int n = snprintf(b, 4096, "Channel,%sCounts (TotalTime=%.1fs%s)\r\n",
                     s->ncal ? "Energy_keV," : "",
                     s->treal, s->tvalid ? "" : "; TIME UNKNOWN");
    httpd_resp_send_chunk(r, b, n);
    for (size_t i = 0; i < s->n; ) {
        int pos = 0;
        for (int j = 0; j < 100 && i < s->n; j++, i++) {
            if (s->ncal) {
                const double x = (double)i;
                double e = 0.0;                 /* по схеме Горнера */
                for (int k = s->ncal - 1; k >= 0; k--) e = e * x + s->cal[k];
                pos += snprintf(b + pos, 4096 - pos, "%u,%.3f,%" PRIu32 "\r\n",
                                (unsigned)i, e, s->d[i]);
            } else {
                pos += snprintf(b + pos, 4096 - pos, "%u,%" PRIu32 "\r\n",
                                (unsigned)i, s->d[i]);
            }
        }
        httpd_resp_send_chunk(r, b, pos);
    }
}

static void exp_n42(httpd_req_t *r, exp_snap_t *s, char *b)
{
    /* UTF-8 BOM, CRLF, без перевода строки в конце,
     * значения ChannelData через пробел с замыкающим пробелом */
    exp_head(r, s, "n42", "application/octet-stream");
    uint32_t r0 = esp_random(), r1 = esp_random();
    uint32_t r2 = esp_random(), r3 = esp_random();
    int n = snprintf(b, 4096,
        "\xEF\xBB\xBF"
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
        "<RadInstrumentData xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\""
        " xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\""
        " n42DocUUID=\"%08lx-%04lx-%04lx-%04lx-%04lx%08lx\""
        " xmlns=\"http://physics.nist.gov/N42/2011/N42\">\r\n",
        (unsigned long)r0,
        (unsigned long)(r1 >> 16), (unsigned long)(r1 & 0xffff),
        (unsigned long)(r2 >> 16), (unsigned long)(r2 & 0xffff),
        (unsigned long)r3);
    httpd_resp_send_chunk(r, b, n);
    if (!s->tvalid)
        httpd_resp_sendstr_chunk(r,
            "  <Remark>TIME UNKNOWN: the device has no clock</Remark>\r\n");
    httpd_resp_sendstr_chunk(r,
        "  <RadInstrumentInformation id=\"RadInstrument\">\r\n"
        "    <RadInstrumentManufacturerName>DIY</RadInstrumentManufacturerName>\r\n"
        "    <RadInstrumentModelName>MCA AD9226</RadInstrumentModelName>\r\n"
        "    <RadInstrumentClassCode>Radionuclide Identifier</RadInstrumentClassCode>\r\n"
        "    <RadInstrumentVersion>\r\n"
        "      <RadInstrumentComponentName>Hardware</RadInstrumentComponentName>\r\n"
        "      <RadInstrumentComponentVersion>ESP32-S3 + AD9226</RadInstrumentComponentVersion>\r\n"
        "    </RadInstrumentVersion>\r\n"
        "  </RadInstrumentInformation>\r\n");
    if (s->ncal) {
        /* EnergyCalibration - после RadInstrumentInformation, до
         * RadMeasurement; спектр ссылается на неё по id */
        int p = snprintf(b, 4096,
            "  <EnergyCalibration id=\"EnergyCalibration\">\r\n"
            "    <CoefficientValues>");
        for (int k = 0; k < s->ncal; k++)
            p += snprintf(b + p, 4096 - p, "%s%.9g", k ? " " : "", s->cal[k]);
        p += snprintf(b + p, 4096 - p,
            "</CoefficientValues>\r\n"
            "  </EnergyCalibration>\r\n");
        httpd_resp_send_chunk(r, b, p);
    }
    n = snprintf(b, 4096,
        "  <RadMeasurement id=\"SpectrumMeasurement-0\">\r\n"
        "    <MeasurementClassCode>Foreground</MeasurementClassCode>\r\n"
        "    <StartDateTime>%02d.%02d.%04d %02d:%02d:%02d</StartDateTime>\r\n"
        "    <RealTimeDuration>PT%luS</RealTimeDuration>\r\n"
        "    <Spectrum id=\"SpectrumData\" radDetectorInformationReference=\"Detector\"%s>\r\n"
        "      <LiveTimeDuration>PT%.1fS</LiveTimeDuration>\r\n"
        "      <ChannelData compressionCode=\"None\">",
        s->ts.tm_mday, s->ts.tm_mon + 1, s->ts.tm_year + 1900,
        s->ts.tm_hour, s->ts.tm_min, s->ts.tm_sec,
        (unsigned long)(s->treal + 0.5),
        s->ncal ? " energyCalibrationReference=\"EnergyCalibration\"" : "",
        s->tlive);
    httpd_resp_send_chunk(r, b, n);
    for (size_t i = 0; i < s->n; ) {
        int pos = 0;
        for (int j = 0; j < 80 && i < s->n; j++, i++)
            pos += snprintf(b + pos, 4096 - pos, "%" PRIu32 " ", s->d[i]);
        httpd_resp_send_chunk(r, b, pos);
    }
    n = snprintf(b, 4096,
        "</ChannelData>\r\n"
        "    </Spectrum>\r\n"
        "    <GrossCounts id=\"GrossForeground\" radDetectorInformationReference=\"Detector\">\r\n"
        "      <TotalCounts>%" PRIu64 "</TotalCounts>\r\n"
        "    </GrossCounts>\r\n"
        "  </RadMeasurement>\r\n"
        "</RadInstrumentData>",
        s->total);
    httpd_resp_send_chunk(r, b, n);
}

static void exp_spe(httpd_req_t *r, exp_snap_t *s, char *b)
{
    /* ENERGY=N,c0,...,cN - полином от младшей степени, N - степень */
    char cal[260] = "";
    if (s->ncal) {
        int p = snprintf(cal, sizeof(cal), "ENERGY=%d", s->ncal - 1);
        for (int k = 0; k < s->ncal; k++)
            p += snprintf(cal + p, sizeof(cal) - p, ",%.9g", s->cal[k]);
        snprintf(cal + p, sizeof(cal) - p, "\r\n");
    }
    /* заголовок KEY=VALUE, маркер SPECTR=, затем счёты uint32
     * little-endian подряд - ESP32 сам little-endian, шлём как есть */
    exp_head(r, s, "spe", "application/octet-stream");
    int pos = snprintf(b, 4096,
        "SHIFR=MCA AD9226\r\n"
        "CONFIGNAME=MCA AD9226\r\n"
        "MEASBEGIN=%02d-%02d-%02d %02d:%02d:%02d.00\r\n"
        "TLIVE=%.2f\r\n"
        "TREAL=%.2f\r\n"
        "DETECTOR=MCA AD9226\r\n"
        "%s"
        "%s"
        "SPECTRSIZE=%u\r\n"
        "SPECTR=",
        s->ts.tm_mday, s->ts.tm_mon + 1, (s->ts.tm_year + 1900) % 100,
        s->ts.tm_hour, s->ts.tm_min, s->ts.tm_sec,
        s->tlive, s->treal,
        s->tvalid ? "" : "COMMENT=TIME UNKNOWN (the device has no clock)\r\n",
        cal, (unsigned)s->n);
    httpd_resp_send_chunk(r, b, pos);
    for (size_t i = 0; i < s->n; ) {
        size_t cnt = s->n - i < 1024 ? s->n - i : 1024;
        httpd_resp_send_chunk(r, (const char *)(s->d + i), cnt * 4);
        i += cnt;
    }
}

/* /export.xml, .csv, .n42, .spe - формат передаётся через user_ctx */
static esp_err_t h_export(httpd_req_t *r)
{
    const char *f = (const char *)r->user_ctx;
    exp_snap_t s;
    if (!exp_snap(r, &s)) return httpd_resp_send_500(r);
    char *b = malloc(4096);
    if (!b) {
        free(s.d);
        return httpd_resp_send_500(r);
    }
    switch (f[0]) {
    case 'x': exp_xml(r, &s, b); break;
    case 'c': exp_csv(r, &s, b); break;
    case 'n': exp_n42(r, &s, b); break;
    default:  exp_spe(r, &s, b); break;
    }
    esp_err_t e = httpd_resp_send_chunk(r, NULL, 0);
    free(b);
    free(s.d);
    return e;
}

static esp_err_t h_css(httpd_req_t *r)
{
    httpd_resp_set_type(r, "text/css; charset=utf-8");
    httpd_resp_set_hdr(r, "Cache-Control", "no-cache");
    return httpd_resp_send(r, CSS, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t h_ljs(httpd_req_t *r)
{
    httpd_resp_set_type(r, "application/javascript; charset=utf-8");
    httpd_resp_set_hdr(r, "Cache-Control", "no-cache");
    return httpd_resp_send(r, LJS, HTTPD_RESP_USE_STRLEN);
}

/* Логотип вшит в прошивку (EMBED_FILES в CMakeLists.txt). */
extern const uint8_t logo_png_start[] asm("_binary_logo_png_start");
extern const uint8_t logo_png_end[]   asm("_binary_logo_png_end");

static esp_err_t h_logo(httpd_req_t *r)
{
    httpd_resp_set_type(r, "image/png");
    /* между прошивками не меняется - пусть браузер держит его в кэше */
    httpd_resp_set_hdr(r, "Cache-Control", "max-age=86400");
    return httpd_resp_send(r, (const char *)logo_png_start,
                           logo_png_end - logo_png_start);
}

/* Значок вкладки - из того же логотипа, 16/32/48 px (на 16 и 32 линии
 * утолщены, иначе орбиты пропадают). */
extern const uint8_t favicon_ico_start[] asm("_binary_favicon_ico_start");
extern const uint8_t favicon_ico_end[]   asm("_binary_favicon_ico_end");

static esp_err_t h_favicon(httpd_req_t *r)
{
    httpd_resp_set_type(r, "image/x-icon");
    httpd_resp_set_hdr(r, "Cache-Control", "max-age=86400");
    return httpd_resp_send(r, (const char *)favicon_ico_start,
                           favicon_ico_end - favicon_ico_start);
}

esp_err_t mca_web_start(void)
{
    net_init();

    httpd_handle_t srv = NULL;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 44;          /* 39 своих + 2 у /ota, с запасом */
    cfg.stack_size       = 8192;
    cfg.core_id          = 0;           /* веб на ядро 0, DSP на ядро 1 */
    cfg.lru_purge_enable = true;

    esp_err_t err = httpd_start(&srv, &cfg);
    if (err != ESP_OK) return err;

    /* именованные поля: при включённом WebSocket у httpd_uri_t есть ещё
     * is_websocket и др., позиционная запись оставляла их без значения */
#define URI(p, h, c) { .uri = p, .method = HTTP_GET, .handler = h, .user_ctx = c }
    httpd_uri_t u[] = {
        URI("/", h_root, NULL),
        URI("/s.css", h_css, NULL),
        URI("/l.js", h_ljs, NULL),
        URI("/logo.png", h_logo, NULL),
        URI("/favicon.ico", h_favicon, NULL),
        URI("/spectrum", h_spectrum, NULL),
        URI("/scope", h_scope, NULL),
        URI("/stat", h_stat, NULL),
        URI("/cps", h_cps, NULL),
        URI("/dose", h_dose, NULL),
        URI("/cfg", h_cfg, NULL),
        URI("/cmd", h_cmd, NULL),
        URI("/settings", h_set_page, NULL),
        URI("/diag", h_diag_page, NULL),
        URI("/log", h_log, NULL),
        URI("/shape", h_shape, NULL),
        URI("/net", h_set_page, NULL),     /* прежние адреса страницы сети */
        URI("/wifi", h_set_page, NULL),
        URI("/net/status", h_net_status, NULL),
        URI("/net/set", h_net_set, NULL),
        URI("/net/confirm", h_net_confirm, NULL),
        URI("/oled.pbm", h_oled, NULL),
        URI("/diag/data", h_diag_data, NULL),
        URI("/diag/set", h_diag_set, NULL),
        URI("/diag/probe", h_diag_probe, NULL),
        URI("/help", h_help, NULL),
        URI("/time", h_time, NULL),
        URI("/hist", h_hist, NULL),
        URI("/calib", h_calib_get, NULL),
        URI("/export.xml", h_export, (void *)"xml"),
        URI("/export.csv", h_export, (void *)"csv"),
        URI("/export.n42", h_export, (void *)"n42"),
        URI("/export.spe", h_export, (void *)"spe"),
    };
#undef URI
    for (int i = 0; i < sizeof(u) / sizeof(u[0]); i++)
        httpd_register_uri_handler(srv, &u[i]);
    static const httpd_uri_t cal_post = {
        .uri = "/calib", .method = HTTP_POST, .handler = h_calib_post,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(srv, &cal_post);

    /* осциллограф по WebSocket */
    s_srv = srv;
    static const httpd_uri_t ws = {
        .uri = "/ws", .method = HTTP_GET, .handler = h_ws,
        .user_ctx = NULL, .is_websocket = true,
    };
    httpd_register_uri_handler(srv, &ws);
    mca_ota_register(srv);              /* обновление прошивки по сети */
    s_codec_ok = scodec_selftest();
    ESP_LOGI(TAG, "сжатие осциллограммы: %s", s_codec_ok
             ? "самопроверка пройдена"
             : "самопроверка НЕ пройдена - кадры идут без сжатия");
    xTaskCreatePinnedToCore(ws_task, "ws_scope", 3072, NULL, 5, NULL, 0);

    ESP_LOGI(TAG, "веб-сервер запущен");
    return ESP_OK;
}
