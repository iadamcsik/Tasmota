import mqtt
import persist
import json
import string

var lamp_enabled = false, geofence_topic, shelly_host, shelly_id, shelly_auth

def lamp_init()
  var time_status = tasmota.cmd('Status 7')['StatusTIM']
  var sunrise = time_status['Sunrise']
  var sunset = time_status['Sunset']
  var current_time = tasmota.strftime('%H:%M', tasmota.rtc()['local'])
  if (current_time < sunrise || sunset < current_time)
    lamp_enabled = true
  end
end
tasmota.add_rule('Time#Minute=%sunset%', def () lamp_enabled = true end)
tasmota.add_rule('Time#Minute=%sunrise%', def () lamp_enabled = false end)
tasmota.add_rule('Time#Initialized', lamp_init)

def shelly_call(action)
  if !tasmota.wifi()['up'] && !tasmota.eth()['up'] return end
  var cl = webclient()
  cl.begin(shelly_host)
  cl.add_header('Content-Type', 'application/x-www-form-urlencoded')
  var r = cl.POST(string.format('id=%s&auth_key=%s&channel=0&turn=%s', shelly_id, shelly_auth, action))
  tasmota.log(string.format('Got response: %s, %s', r, cl.get_string()))
end

def switch_lamp(topic, idx, payload_s)
  var transition = json.load(payload_s)
  if (transition["desc"] == "Home")
    if (transition["event"] == "enter" && lamp_enabled)
      shelly_call('on')
    elif (transition["event"] == "leave")
      shelly_call('off')
    end
  end
end

var red_lower, red_upper, green_lower, green_upper, ifttt_path, alert_threshold = -1, ifttt_host = "https://maker.ifttt.com/trigger/"

tasmota.set_power(1, false)
tasmota.set_power(0, true)

def do_alert()
  if !tasmota.wifi()['up'] && !tasmota.eth()['up'] return end
  var cl = webclient()
  cl.begin(ifttt_host + ifttt_path)
  var r = cl.POST("")
  tasmota.log(string.format('Got response: %s, %s', r, cl.get_string()))
end

def check_color(hue)
  if (red_lower < hue && hue < red_upper)
    if (alert_threshold < 0)
      tasmota.set_power(1, true)
      tasmota.set_power(0, false)
      alert_threshold = tasmota.millis(1799000)
    end
    if tasmota.time_reached(alert_threshold)
      do_alert()
      alert_threshold = tasmota.millis(1799000)
    end
  end
  if (green_lower < hue && hue < green_upper)
    tasmota.set_power(1, false)
    tasmota.set_power(0, true)
    alert_threshold = -1
  end
end

if (persist.has('lamp_init'))
  geofence_topic = persist.geofence_topic
  shelly_host = persist.shelly_host
  shelly_id = persist.shelly_id
  shelly_auth = persist.shelly_auth
  mqtt.subscribe(geofence_topic, switch_lamp)
end

if (persist.has('monitor_init'))
  red_lower = persist.red_lower
  red_upper = persist.red_upper
  green_lower = persist.green_lower
  green_upper = persist.green_upper
  ifttt_path = persist.ifttt_path
  tasmota.add_rule('Tele-TCS34725#H', def (value) check_color(value) end)
end
