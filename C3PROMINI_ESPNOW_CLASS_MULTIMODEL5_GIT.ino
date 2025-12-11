/*
    ESP-NOW Adhoc Peer v1
    Cloud Power Electric Industries（雲力電工） - 2025

    特徴
    ESP_NOW_Peerクラスを使った、アドホックネットワークプログラムだよ。
    ESP_NOWだから、AP不要で、空間内にあるESP32同士を自動でユニキャスト接続できるよ。
    ESP_NOW　V2の環境なら、 1470byteまで遅れるよ。コンパイル環境がV2かどうかは、起動時にシリアルで確認してね。
    グループUUIDが同じ端末を自動でピアリングして、ユニキャストで通信するよ。
    サーバー1台で他はクライアントだけのスター型のシステムはもちろん、サーバー複数かつクライアント複数の複雑なアドホックも簡単にできるよ。
    設定を全部サーバーにすれば、全員が双方向でつながるよ。
    ハートビートでクライアントの状態監視も行い、接続が切れた場合も、切れたESP32の環境が復旧すれば自動接続するよ。
    設定に必要なのは、各ESP32ごとに、クライアントと、サーバー、グループUUIDだけのシンプル設計。
    UUIDをグループIDにして自動接続しているから、他のグループとの衝突はほぼないよ。
    暗号化モードにも対応。暗号化モード設定と鍵を設定するだけで使えるよ。ただし、暗号化の時は台数が少ないから気をつけてね。
    
    安全性
    暗号化モードでのユニキャスト時の安全性を強化
    登録アドバタイジングのグループUUIDとピア登録後のユニキャストのUUIDは別にしているので、暗号化モードの場合、ユニキャストのUUIDは暗号化して公開されないから安全だよ
  
    Flow:
    1. ブロードキャストでアドバタイジングパケットの送信（アドバタイジンググループUUIDを利用）
    2. 同一グループUUIDの端末が受信すると、自動ピア登録（グループUUIDを利用）
    3. 登録後はユニキャスト通信でハートビートを送って、通信を確立


    このモデルに、センサーを組み合わせてIOTメッシュネットワークを作るも、アドホックで災害時に活躍するドローンロボットを作るも、クラサバのラジコンを作るも、君次第だよ。


*/
#include "ESP32_NOW.h"
#include "WiFi.h"
#include "esp_mac.h"  // For the MAC2STR and MACSTR macros

//WIFIチャンネルを設定。グループは全部同じチャンネルでないと接続できないよ。
#define ESPNOW_WIFI_CHANNEL 4  // 1 - 14

//ピア接続のグループIDは同じグループだけ接続できるよ
#define ADV_GROUP_ID "906b868f-7e9b-4c21-b587-70c8d5fadfee"  // （アドバタイジンググループUUID）登録用アドバタイジングパケットで使うグループのUUIDを指定してね。
#define GROUP_ID "73f8e3bb-aab2-4808-8efe-c061c88e48c2"      // （グループUUID）ユニキャストで使うグループのUUIDを指定してね。

/*この端末がクライアントかサーバーかを選択。グループに必ず１台はサーバーがないと繋がらないよ*/
#define ROLL 1  //1サーバー 0クライアント

#define SECURITY 1  //1暗号化あり（1台は5台まで接続可能）だよ。  0暗号化なし（1台は19台までピア接続可能）だよ。

#define ESPNOW_PMK_STRING "hogehoge54321"  //暗号化モードでは、この公開鍵を設定してね
#define ESPNOW_LMK_STRING "hogehoge12345"  //暗号化モードでは、この秘密鍵を設定してね

//ここまでの設定をしたESP32を複数用意すればテストできるよ。



// システム設定
#define HEARTBEAT_TIMEOUT (5000)  //ms PINGがこの時間来なければタイムアウトするよ

/*コマンド、相手に追加したいコマンドをここに追加してね*/
#define CMD_REGISTER 1
#define CMD_HEARTBEAT 2
//ここまでは変更しないでね。ここから下にコマンド追加して、送信を処理できるよ
#define CMD_DATA 11  //たとえば

// 送信パケットの構造体だよ。ESP-NOW 2の環境なら、この型で1470byteを越えなければOK。送信時のメッセージバイトを確認してね。
typedef struct __attribute__((packed)) {
  char group_id[37];  //グループのUUIDが入るよ
  bool role;
  uint8_t channel;
  uint8_t cmd;
  char data[1000];  //容量内で好きに追加可能(この場合は英文1000文字までだよ)
  //char sensor_data[20];  //容量内で好きに追加可能(この場合は英文20文字までだよ)
  uint8_t checksum;
} espnow_message_t;

bool isServer = ROLL;
bool isSecurity = SECURITY;
int ledPin = 8;

//ピア接続を扱うための肝のクラスだよ。ピアごとにこのインスタンスを立ち上げるよ
class ADHOC_ESP_NOW_Peer : public ESP_NOW_Peer {
public:
  ADHOC_ESP_NOW_Peer(const uint8_t *mac_addr, uint8_t channel, wifi_interface_t iface, const uint8_t *lmk)
    : ESP_NOW_Peer(mac_addr, channel, iface, lmk) {}
  ~ADHOC_ESP_NOW_Peer() {
    remove();
  }
  bool Remove() {
    return remove();
  }
  bool Begin() {
    return add();
  }
  bool Send(const uint8_t *data, size_t len) {
    return send(data, len);
  }
  //受信成功した場合、コールバックされるよ
  void onSent(bool success) {
    if (success) {
      Serial.println("Send Success");
    } else {
      Serial.println("Send Failed");
    }
  }

  unsigned long LastGetms;  //ハートビート信号の受信時間を保持に使うよ
  bool IsServer;            //true=サーバー、false=クライアントを記録するよ
  //受信した場合、コールバックされる（ここにはユニキャストも、ブロードキャストもピアされているパケットのみ入ってくるよ）
  void onReceive(const uint8_t *data, size_t len, bool broadcast) {

    espnow_message_t *getUnicastMsg = (espnow_message_t *)data;  //受信データをデータ構造に復元

    if ((isServer == true) || (isServer == false && getUnicastMsg->role == true)) {  //ここで受信するパケットはサーバーroleのみだよ。悪いけどもしお隣のクライアントroleのパケが来ても無視だよ

      if (getUnicastMsg->group_id == std::string(GROUP_ID)) {  //グループIDが同じクライアントのみ処理するよ
        //相手のMACアドレスを確認するよ
        const uint8_t *mac = addr();
        switch (getUnicastMsg->cmd) {  //コマンドで分岐するよ
          case CMD_HEARTBEAT:          //ハートビートパケットを受け入れて相手の生存時間を更新するよ。CLASSモデルは、特にMACアドレスとかの検索とかしなくても、これだけで登録できるのは楽だね
            LastGetms = millis();      //登録時間を記録するよ
            Serial.printf("[GET] [%s] (%02X:%02X:%02X:%02X:%02X:%02X) CMD_SERVER_HEARTBEAT DATA = %s\n", (broadcast ? "Broadcast" : "Unicast"), mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], getUnicastMsg->data);

            break;
          case CMD_DATA:
            //todo ユニキャスト登録したレジスターは基本来ないので削除可能だよ
            Serial.printf("[GET] [%s] Peer Receive(%02X:%02X:%02X:%02X:%02X:%02X) CMD_DATA DATA = %s\n", (broadcast ? "Broadcast" : "Unicast"), mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], getUnicastMsg->data);
            break;
            //コマンドを増やしたら、以下にCaseを追加して処理を追加できるよ
        }
      }
    }
  }
};


//ピアした台数分のインスタンスをリストにして保持するよ
std::vector<ADHOC_ESP_NOW_Peer *> clients;

//インスタンスの最初はnullptrしとかないとうまくいかないよ
ADHOC_ESP_NOW_Peer *espnow_peer_broadcast = nullptr;
ADHOC_ESP_NOW_Peer *espnow_peer = nullptr;

//登録済みのクライアントピア数をカウントする機能だよ
int getServerPeerNum() {
  int ServerPeerNum = 0;
  for (size_t i = 0; i < clients.size(); i++) {
    if (clients[i]->IsServer == true) {
      ++ServerPeerNum;
    }
  }
  return ServerPeerNum;
}

//登録済みのサーバーピア数をカウントする機能だよ
int getClientPeerNum() {
  int ClientPeerNum = 0;
  for (size_t i = 0; i < clients.size(); i++) {
    if (clients[i]->IsServer == false) {
      ++ClientPeerNum;
    }
  }
  return ClientPeerNum;
}

//ピア登録する機能だよ
void registPeer(const esp_now_recv_info_t *info, const uint8_t *data) {
  espnow_message_t *getRegistMsg = (espnow_message_t *)data;  //受信データをデータ構造に復元
  //ピアをインスタンスとして起動するよ
  if (isSecurity == true) {
    espnow_peer = new ADHOC_ESP_NOW_Peer(info->src_addr, ESPNOW_WIFI_CHANNEL, WIFI_IF_STA, (const uint8_t *)ESPNOW_LMK_STRING);
  } else {
    espnow_peer = new ADHOC_ESP_NOW_Peer(info->src_addr, ESPNOW_WIFI_CHANNEL, WIFI_IF_STA, NULL);
  }


  espnow_peer->Begin();
  espnow_peer->IsServer = getRegistMsg->role;  //サーバーか、クライアントかをフラグするよ
  espnow_peer->LastGetms = millis();           //登録時間を記録するよ

  //ピア登録を表示
  const uint8_t *mac = info->src_addr;
  Serial.printf("[REG] [Bloadcast] (%02X:%02X:%02X:%02X:%02X:%02X) CMD__REGISTER TYPE: [%s] DATA: %s\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], (getRegistMsg->role ? "SERVER" : "CLIENT"), getRegistMsg->data);
  Serial.println("[REG] おおー、おかえり！たくさん話そうね");
  //起動したこのインスタンスをリストに入れるよ
  clients.push_back(espnow_peer);
}

//ここはピア登録に利用するよ。この関数はピアなしのパケットがコールバックされるよ
void espnow_regist(const esp_now_recv_info_t *info, const uint8_t *data, int len, void *arg) {
  espnow_message_t *getRegistMsg = (espnow_message_t *)data;  //受信データをデータ構造に復元




  if (getRegistMsg->group_id == std::string(ADV_GROUP_ID)) {  //グループIDが同じクライアントのみ処理するよ

    if (isServer == true) {              //この端末がサーバーさんの場合、
      if (getRegistMsg->role == true) {  //サーバーロールさんが登録申請に来たので登録するよ
        registPeer(info, data);
      }
      if (getRegistMsg->role == false) {  //クライアントロールさんも登録申請に来たので登録するよ
        registPeer(info, data);
      }
    }

    if (isServer == false) {             //この端末がクライアントさんの場合、
      if (getRegistMsg->role == true) {  //サーバーロールさんが登録申請にので登録するよ
        registPeer(info, data);
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  WiFi.mode(WIFI_STA);
  WiFi.setChannel(ESPNOW_WIFI_CHANNEL);
  WiFi.setTxPower(WIFI_POWER_17dBm);
  while (!WiFi.STA.started()) {
    delay(100);
  }
  //この端末のWIFI情報を表示
  Serial.println("ESP-NOW Broadcast Master");
  Serial.println("Wi-Fi parameters:");
  Serial.println("  Mode: STA");
  Serial.println("  MAC Address: " + WiFi.macAddress());
  Serial.printf("  Channel: %d\n", ESPNOW_WIFI_CHANNEL);

  if (isSecurity == true) {  //セキュリティーモードの場合だよ

    if (!ESP_NOW.begin((const uint8_t *)ESPNOW_PMK_STRING)) {  //もし、ESP＿NOWが立ち上がらない（Wifi異常とか？）ならエラーを返すよ（たぶん機械異常とか？）
      Serial.println("Failed to initialize ESP-NOW");
      Serial.println("Reeboting in 5 seconds...");
      delay(5000);
      ESP.restart();
    } else {
      //この端末のESP_NOW情報を表示
      Serial.println("ESP-NOW paramaters:");
      Serial.print("  Advertising Group UUID: ");
      Serial.println(ADV_GROUP_ID);
      Serial.print("  Group UUID: ");
      Serial.println(GROUP_ID);
      Serial.printf((isSecurity ? "Secure Mode" : "[No Secure Mode]"));
      Serial.printf("  This is [%s] Mode \n", (isServer ? "SERVER" : "CLIENT"));
      Serial.printf("  ESP-NOW version: %d, max data length: %d\n", ESP_NOW.getVersion(), ESP_NOW.getMaxDataLen());
    }

  } else {                   //セキュリティーモード無しの場合だよ
    if (!ESP_NOW.begin()) {  //もし、ESP＿NOWが立ち上がらない（Wifi異常とか？）ならエラーを返すよ（たぶん機械異常とか？）
      Serial.println("Failed to initialize ESP-NOW");
      Serial.println("Reeboting in 5 seconds...");
      delay(5000);
      ESP.restart();
    } else {
      //この端末のESP_NOW情報を表示
      Serial.println("ESP-NOW paramaters:");
      Serial.print("  Advertising Group UUID: ");
      Serial.println(ADV_GROUP_ID);
      Serial.print("  Group UUID: ");
      Serial.println(GROUP_ID);
      Serial.printf((isSecurity ? "Secure Mode" : "No Secure Mode"));
      Serial.printf("  This is %s Mode \n", (isServer ? "SERVER" : "CLIENT"));
      Serial.printf("  ESP-NOW version: %d, max data length: %d\n", ESP_NOW.getVersion(), ESP_NOW.getMaxDataLen());
    }
  }


  //レジスト用のコールバックを設定するよ。ピアなしのブロードキャストを受信して登録に使うよ
  ESP_NOW.onNewPeer(espnow_regist, NULL);

  //ブロードキャスト用のインスタンスを起動するよ
  espnow_peer_broadcast = new ADHOC_ESP_NOW_Peer(ESP_NOW.BROADCAST_ADDR, ESPNOW_WIFI_CHANNEL, WIFI_IF_STA, NULL);
  espnow_peer_broadcast->Begin();



  if (isServer == true) {  //SERVERはLEDを青く光らせるよ（動作には不要だから適宜変えてね）
    pinMode(ledPin, OUTPUT);
    digitalWrite(ledPin, LOW);
    Serial.println("LED ON");
    vTaskDelay(pdMS_TO_TICKS(1000));
    digitalWrite(ledPin, HIGH);
    Serial.println("LED OFF");
  }
}


void loop() {

  //ユニキャスト登録用のデータを準備して、ブロードキャストでアドバタイジングパケットを送信するよ（クライアントまたはサーバーが複数台あるときは常に出しておく必要があるが、台数が限定なら、一定数達したらひっこめた方が安全だね）
  espnow_message_t registMsg;
  memset(&registMsg, 0, sizeof(registMsg));
  strncpy(registMsg.group_id, ADV_GROUP_ID, sizeof(registMsg.group_id));                      //ADV_GROUP_IDで登録要求するよ。テキスト（char）はstrncpyを使う
  registMsg.channel = ESPNOW_WIFI_CHANNEL;                                                    //WIFIチャンネル（全台共通で設定するよ）
  registMsg.role = ROLL;                                                                      //これはこの端末がサーバーか、クライアントかだよ。
  registMsg.cmd = CMD_REGISTER;                                                               //登録を要求するコマンドだよ
  strncpy(registMsg.data, "僕を登録してくれるか？お互い様だよね？", sizeof(registMsg.data));  //送信したい内容だよ。（無くてもOK）
  registMsg.checksum = sizeof(registMsg);
  espnow_peer_broadcast->Send((uint8_t *)&registMsg, sizeof(registMsg));  //送信！登録よろしくー
  Serial.println("[SEND] [Broadcast] CMD_REGISTER : メッセージバイト数[" + String(sizeof(registMsg)) + "]");

  if (espnow_peer == nullptr) {
    //Non Client
    Serial.println("ピアが誰も見つからないよ。俺はぼっちか。ちょー寂しいから相手の電源を入れてね");

  } else {
    //ここからハートビートを送信
    //つないでいるピア相手にハートビート信号を一定時間で通知を送り、相手が一定期間、ESPNOWのピアを解除するよ

    //各ピアのインスタンスは、clientsというリストにあって、forで順番に呼び出してインスタンスごとにそれぞれのピアに送るよ。
    for (size_t i = 0; i < clients.size(); i++) {
      if (clients[i]) {

        //ここからハートビートパケットを準備して送るよ
        espnow_message_t HAMsg;
        memset(&HAMsg, 0, sizeof(HAMsg));                           //配信に必要なメモリー空間を割り当てするよ。
        strncpy(HAMsg.group_id, GROUP_ID, sizeof(HAMsg.group_id));  //GROUP_IDで登録要求するよ。テキスト（char）はstrncpyを使う
        HAMsg.channel = ESPNOW_WIFI_CHANNEL;
        HAMsg.role = ROLL;
        HAMsg.cmd = CMD_HEARTBEAT;
        strncpy(HAMsg.data, "僕は生きてるが、お前生きてるか？", sizeof(HAMsg.data));  //HAの追加データ時、送信したい内容だよ。テキスト（char）はstrncpyを使う
        HAMsg.checksum = sizeof(HAMsg);
        clients[i]->Send((uint8_t *)&HAMsg, sizeof(HAMsg));  //送信！登録よろしくー
        Serial.printf("[SEND] [Unicast] CMD_SERVER_HEARTBEAT %zu: " MACSTR "\n", i, MAC2STR(clients[i]->addr()));

        //ついでに相手からのHAが来てないと分かったら、登録を抹消して消えてもらうぞ。さよなら
        if (HEARTBEAT_TIMEOUT < (millis() - clients[i]->LastGetms)) {
          Serial.println("[REG] まじか、あいつが死んだ。生き返るの待つよ。");
          clients[i]->Remove();                // Remove Instance
          clients.erase(clients.begin() + i);  // Remove Instance
          exit;
        }
      }
    }
    //サーバーと、クライアントのピア数表示だよ
    if (isServer) {
      Serial.println("[REG] 現在のサーバーピア数[" + String(getServerPeerNum()) + "]");
      Serial.println("[REG] 現在のクライアントピア数[" + String(getClientPeerNum()) + "]");
    } else {
      Serial.println("[REG] 現在のサーバーピア数[" + String(getServerPeerNum()) + "]");
    }
  }

  delay(1000);  //ハートビートパケットは1秒に一回送信することにしているよ
}



uint8_t calculateChecksum(const uint8_t *data, size_t len) {
  uint8_t checksum = 0;
  for (size_t i = 0; i < len; i++) {
    checksum = (checksum << 1) ^ data[i];
  }
  return checksum;
}


