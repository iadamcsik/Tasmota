import mqtt
import persist
import json
import string

var lamp_enabled = false, geofence_topic, shelly_host, shelly_id, shelly_auth, sunrise, sunset

def adjust_daylight()
  var time_status = tasmota.cmd('Status 7')['StatusTIM']
  sunrise = string.split(time_status['Sunrise'], ':')
  sunset = string.split(time_status['Sunset'], ':')
  tasmota.remove_cron("sunset")
  tasmota.remove_cron("sunrise")
  tasmota.add_cron(string.format('0 %d %d * * *', int(sunset[1]), int(sunset[0])), def () lamp_enabled = true end, "sunset")
  tasmota.add_cron(string.format('0 %d %d * * *', int(sunrise[1]), int(sunrise[0])), def () lamp_enabled = false end, "sunrise")
end

def lamp_init()
  var time_status = tasmota.cmd('Status 7')['StatusTIM']
  sunrise = time_status['Sunrise']
  sunset = time_status['Sunset']
  var current_time = tasmota.strftime('%H:%M', tasmota.rtc()['local'])
  if (current_time < sunrise || sunset < current_time)
    lamp_enabled = true
  end
  adjust_daylight()
end

tasmota.add_rule('Time#Initialized', lamp_init)
tasmota.add_cron('0 0 0 * * 0', adjust_daylight)

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

var red_lower, red_upper, green_lower, green_upper, brevo_key, alert_millis = nil, alert_timeout_min = 30, brevo_endpoint = 'https://api.brevo.com/v3/smtp/email', brevo_to, brevo_to_name
var brevo_rq_template = '{ "subject":"\\uD83D\\uDEA8 Hibajelzés \\uD83D\\uDEA8", "sender": {"name":"Hőszivattyú", "email":"hoszivattyu@vintner.hu" }, "htmlContent":"A hőszivattyú legalább 30 perce hibát jelez!", "to":[ { "email":"%s", "name":"%s" } ] }'

tasmota.set_power(1, false)
tasmota.set_power(0, true)

def do_alert()
  if !tasmota.wifi()['up'] && !tasmota.eth()['up'] return end
  var cl = webclient()
  cl.begin(brevo_endpoint)
  cl.add_header('api-key', brevo_key)
  cl.add_header('content-type', 'application/json')
  var r = cl.POST(string.format(brevo_rq_template, brevo_to, brevo_to_name))
  tasmota.log(string.format('Got response: %s, %s', r, cl.get_string()))
end

def check_color(hue)
  if (red_lower < hue || hue < red_upper)
    if (alert_millis == nil)
      tasmota.set_power(1, true)
      tasmota.set_power(0, false)
      # subtracting a sec to make sure alert is fired at 30 min not 35 (assuming a 5 min tele period)
      alert_millis = tasmota.millis(alert_timeout_min * 60000 - 1000)
    elif tasmota.time_reached(alert_millis)
      do_alert()
      # setup another round of alert
      alert_millis = tasmota.millis(alert_timeout_min * 60000 - 1000)
    end
  end
  if (alert_millis != nil && green_lower < hue && hue < green_upper)
    tasmota.set_power(1, false)
    tasmota.set_power(0, true)
    alert_millis = nil
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
  brevo_key = persist.brevo_key
  brevo_to = persist.brevo_to
  brevo_to_name = persist.brevo_to_name
  tasmota.add_rule('Tele#TCS34725#H', def (value) check_color(value) end)
end
