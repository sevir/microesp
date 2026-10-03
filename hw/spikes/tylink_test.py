import os, time, json, hmac, hashlib, ssl, uuid, sys
import paho.mqtt.client as mqtt
env = dict(l.strip().split("=", 1) for l in open(os.path.expanduser("~/.config/microesp/tuyalink.env")) if "=" in l)
did, sec, reg = env["TUYALINK_DEVICE_ID"], env["TUYALINK_DEVICE_SECRET"], env["TUYALINK_REGION"]
host = {"cn": "m1.tuyacn.com", "eu": "m1.tuyaeu.com", "us": "m1.tuyaus.com", "in": "m1.tuyain.com"}[reg]
ts = str(int(time.time()))
user = f"{did}|signMethod=hmacSha256,timestamp={ts},secureMode=1,accessType=1"
content = f"deviceId={did},timestamp={ts},secureMode=1,accessType=1"
pw = hmac.new(sec.encode(), content.encode(), hashlib.sha256).hexdigest().rjust(64, "0")
base = f"tylink/{did}/thing"
def mid(): return uuid.uuid4().hex[:16]
def now(): return int(time.time() * 1000)
c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"tuyalink_{did}")
c.username_pw_set(user, pw)
c.tls_set(cert_reqs=ssl.CERT_REQUIRED)
def on_connect(cl, ud, flags, rc, props=None):
    print("CONNECT rc:", rc)
    if rc == 0:
        for t in ["property/set", "action/execute", "model/get_response", "property/report_response", "model/get/response"]+["../channel/#"]:
            cl.subscribe((f"{base}/{t}" if not t.startswith("..") else f"tylink/{did}/"+t[3:]), qos=1)
        cl.publish(f"{base}/model/get", json.dumps({"msgId": mid(), "time": now(), "data": {"format": "complex"}}), qos=1)
def on_message(cl, ud, m):
    p = m.payload.decode(errors="replace")
    print("RX", m.topic.replace(did, "<dev>"), p[:3000])
    if m.topic.endswith("property/set") or m.topic.endswith("action/execute"):
        try: j = json.loads(p)
        except Exception: return
        cl.publish(m.topic + "_response", json.dumps({"msgId": j.get("msgId"), "time": now(), "code": 0}), qos=1)
def on_disconnect(cl, ud, flags, rc, props=None): print("DISCONNECT rc:", rc)
def on_subscribe(cl, ud, mid_, rcs, props=None): print("SUBACK", mid_, [str(r) for r in rcs])
def on_publish(cl, ud, mid_, rc, props=None): print("PUBACK", mid_, rc)
c.on_subscribe=on_subscribe; c.on_publish=on_publish
c.on_connect, c.on_message, c.on_disconnect = on_connect, on_message, on_disconnect
print("host", host)
c.connect(host, 8883, keepalive=60)
c.loop_start()
time.sleep(8)
props = json.loads(sys.argv[1]) if len(sys.argv) > 1 else {}
if props:
    t = now()
    c.publish(f"{base}/property/report", json.dumps({"msgId": mid(), "time": t, "data": {k: {"value": v, "time": t} for k, v in props.items()}}), qos=1)
    print("TX report", props)
time.sleep(int(os.environ.get("WAIT", "15")))
c.loop_stop(); c.disconnect()
