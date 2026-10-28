"""Прошивка прибора по сети: python tools/ota.py <ip> [файл.bin]

Шлёт build/esp32_mca.bin на /ota, ждёт перезапуска и запрашивает /ota
у новой прошивки - этот запрос её и подтверждает (иначе следующая
перезагрузка вернёт прежнюю, см. main/mca_ota.c). Только стандартная
библиотека Python."""
import json
import os
import sys
import time
import urllib.error
import urllib.request


def info(base, timeout=3):
    return json.load(urllib.request.urlopen(base + "/ota", timeout=timeout))


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    host = sys.argv[1]
    base = host if host.startswith("http") else "http://" + host
    path = sys.argv[2] if len(sys.argv) > 2 else os.path.join(
        os.path.dirname(__file__), "..", "build", "esp32_mca.bin")
    data = open(path, "rb").read()

    try:
        old = info(base)
    except Exception as e:
        sys.exit(f"прибор {base} не отвечает на /ota: {e}\n"
                 "(прошивка без обновления по сети - один раз прошейте по USB)")
    print(f"сейчас: {old['ver']} {old['date']} {old['time']}, слот {old['run']}"
          f" -> пишу в {old['next']} ({len(data)} байт из {old['size']})")

    req = urllib.request.Request(base + "/ota", data=data, method="POST",
                                 headers={"Content-Type": "application/octet-stream"})
    t0 = time.time()
    try:
        r = json.load(urllib.request.urlopen(req, timeout=120))
    except urllib.error.HTTPError as e:
        body = e.read().decode("utf-8", "replace")
        try:
            body = json.loads(body)["msg"]
        except Exception:
            pass
        sys.exit(f"прибор отказал ({e.code}): {body}")
    print(f"{r['msg']} ({time.time() - t0:.1f} с)")
    if not r.get("ok"):
        sys.exit(1)

    time.sleep(3)
    for _ in range(30):
        try:
            new = info(base)
            break
        except Exception:
            time.sleep(1)
    else:
        sys.exit("прибор не вышел на связь за 30 с; если он так и не поднимется, "
                 "следующая перезагрузка вернёт прежнюю прошивку")
    print(f"работает: {new['ver']} {new['date']} {new['time']}, слот {new['run']},"
          f" состояние {new['state']}, сброс {new.get('reset', '?')}")
    if new["run"] == old["run"]:
        sys.exit("слот не сменился - новая прошивка не загрузилась (откат?)")
    # запрос выше подтвердил прошивку; проверим, что так и записалось
    st = info(base)["state"]
    print("подтверждена" if st == "valid" else f"состояние после подтверждения: {st}")


if __name__ == "__main__":
    main()
