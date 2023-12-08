import mqtt
import persist
import json
import string

var lamp_enabled = false, lamp_topic, shelly_host, shelly_id, shelly_auth

tasmota.add_rule('Time#Minute=%sunset%', def () lamp_enabled = true end)
tasmota.add_rule('Time#Minute=%sunrise%', def () lamp_enabled = false end)
var time_status = tasmota.cmd('Status 7')['StatusTIM']
var sunrise = time_status['Sunrise']
var sunset = time_status.['Sunset']
var current_time = tasmota.strftime('%H:%M', tasmota.rtc().local)
if (current_time < sunrise || sunset < current_time)
  lamp_enabled = true
end

if (persist.has('lamp_init'))
  mqtt.subscribe(persist.lamp_topic, switch_lamp)
end

def switch_lamp(topic, idx, payload_s)
  var transition = json.load(payload_s)
  if (transition.desc == "Home")
    if (transition.event == "enter" && lamp_enabled)
      shelly_call('on')
    elif (transition.event == "leave")
      shelly_call('off')
    end
  end
end 

def shelly_call(action)
  if !tasmota.wifi()['up'] && !tasmota.eth()['up'] return end
  var cl = webclient()
  cl.add_header('Content-Type', 'application/x-www-form-urlencoded')
  cl.begin(shelly_host)
  var r = cl.POST(string.format('id=%s&auth_key=%s&channel=0&turn=%s'), shelly_id, shelly_auth, action)
  tasmota.log(string.format('Got response: %s, %s', r, cl.get_string()))
end
