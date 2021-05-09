# 事前準備

## 更改 esp-idf

![image](https://user-images.githubusercontent.com/1200981/117580273-ce6da600-b129-11eb-94ec-f9f23b02a33c.png)

![image](https://user-images.githubusercontent.com/1200981/117580286-d9283b00-b129-11eb-856b-5678c996cbe1.png)

![image](https://user-images.githubusercontent.com/1200981/117580297-e513fd00-b129-11eb-95e3-e8ddf6091ccd.png)

## e-wristband

### 若要上傳給 relay node, 

直接上傳 e-wristband

### 若要上傳給發送 node

更改 wifi 密碼, 用於獲取時間

![image](https://user-images.githubusercontent.com/1200981/117580355-2b695c00-b12a-11eb-9673-0126fb74cff1.png)

取代 `0x000d` 為你的發送 node 的 ble mesh address

![image](https://user-images.githubusercontent.com/1200981/117580474-ab8fc180-b12a-11eb-90e6-0936b10852c2.png)

uncomment 掉這些程式碼，以一開始用 wifi 獲取時間

![image](https://user-images.githubusercontent.com/1200981/117580518-e09c1400-b12a-11eb-940a-b4d5eaad6531.png)

## gateway

需要以 usb 連接電腦

更改 ssid

![image](https://user-images.githubusercontent.com/1200981/117580572-1b9e4780-b12b-11eb-9b81-a2b807f122f3.png)

更改 coap server 地址

![image](https://user-images.githubusercontent.com/1200981/117580724-f231eb80-b12b-11eb-8777-894d6f796687.png)

