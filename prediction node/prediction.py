import paho.mqtt.client as mqtt
from statsmodels.tsa.arima_model import ARIMA

train = list()

def on_connect(client, userdata, flags, rc):
    print("Connected with result code " + str(rc))

    client.subscribe("temp")

# The callback for when a PUBLISH message is received from the server.
def on_message(client, userdata, msg):
    print(msg.topic + " " + str(msg.payload))
    print(int(msg.payload))
    train.append(int(msg.payload))
    # print(len(train))
    #check when start prediction, data befor are for learning model
    if (len(train) >= 5):
        print("start prediction....")
        model = ARIMA(train, order=(5, 1, 0))
        print("predicting...")
        model_fit = model.fit(disp=0)
        output = model_fit.forecast()
        yhat = output[0]
        predictions.append(yhat)
        print(yhat)
        #check precedente prediction value with temperature, if difference is more then 5, send alert
        if(abs(predictions[len(prediction)] - int(msg.payload)) > 5):
            client.publish("temp", "ALERT", qos=0)
    
def on_publish(client, userdata, mid):
    print("mid: " + str(mid))

def predict(client):
    print(len(train))
    
client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message
client.on_publish = on_publish

client.username_pw_set("mqtt", "2MFfprU2W")

client.connect("histella.myds.me", 1883, 60)

client.loop_forever()
