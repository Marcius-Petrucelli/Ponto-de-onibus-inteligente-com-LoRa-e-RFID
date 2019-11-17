#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <MFRC522.h>
#include <DFMiniMp3.h>
#include <Keypad.h>


#define LORA_SCK     5
#define LORA_MISO    19
#define LORA_MOSI    27
#define LORA_SS      18
#define LORA_RST     14
#define LORA_DI0     26
#define LORA_BAND    915E6

byte localAddress = 108;     // Este sou eu!!!!! 
byte destination = 0xFF;     // Destino original broadcast (0xFF broad)
byte msgCount = 0; 

#define RFID_SDA 5
#define RFID_SCK 18
#define RFID_MOSI 27
#define RFID_MISO 19
#define RFID_RST 14

#define SIZE_BUFFER     18
#define MAX_SIZE_BLOCK  16

long tempoEspera1 = 2500;
long tempoEspera2 = 8000;

int linhas[] = {2, 41, 54, 101}; //linhas que passam pelo ponto
int vizinhos[]={109, 110, 111}; //vizinhos que antecedem este ponto
int num = 0;
int requisitadas[] = {0,0,0,0};  // ultimas linhas requisitadas
int idLinha;
int requer;
String str;
String key_linhas;

MFRC522::MIFARE_Key key;
MFRC522::StatusCode status;

MFRC522 mfrc522(RFID_SDA, RFID_RST);  // Create MFRC522 instance
int current_spi = -1; // -1 - NOT STARTED   0 - RFID   1 - LORA

class Mp3Notify
{
  public:
    static void OnError(uint16_t errorCode)
    {
      // see DfMp3_Error for code meaning
      Serial.print("Com Error ");
      Serial.println(errorCode);
    }
    static void OnPlayFinished(uint16_t track)
    {
      Serial.print("Play finished for #");
      Serial.println(track);
    }
    static void OnCardOnline(uint16_t code)
    {
      Serial.println("Card online ");
    }
    static void OnUsbOnline(uint16_t code)
    {
      Serial.println("USB Disk online ");
    }
    static void OnCardInserted(uint16_t code)
    {
      Serial.println("Card inserted ");
    }
    static void OnUsbInserted(uint16_t code)
    {
      Serial.println("USB Disk inserted ");
    }
    static void OnCardRemoved(uint16_t code)
    {
      Serial.println("Card removed ");
    }
    static void OnUsbRemoved(uint16_t code)
    {
      Serial.println("USB Disk removed ");
    }
};

HardwareSerial mySerial(2);
DFMiniMp3<HardwareSerial, Mp3Notify> mp3(mySerial);

const byte ROWS = 4; //four rows
const byte COLS = 3; //three columns
char keys[ROWS][COLS] = {
  {'1', '2', '3'},
  {'4', '5', '6'},
  {'7', '8', '9'},
  {'*', '0', '#'}
};


byte rowPins[ROWS] = {15, 25, 12, 4}; //connect to the row pinouts of the keypad
byte colPins[COLS] = {2, 23, 13}; //connect to the column pinouts of the keypad

Keypad keypad = Keypad( makeKeymap(keys), rowPins, colPins, ROWS, COLS );

LiquidCrystal_I2C lcd(0x27,2,1,0,4,5,6,7,3, POSITIVE);

void spi_select(int which) {
  if (which == current_spi) return;
  SPI.end();

  switch (which) {
    case 0:
      SPI.begin(RFID_SCK, RFID_MISO, RFID_MOSI);
      mfrc522.PCD_Init();
      break;
    case 1:
      SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
      LoRa.setPins(LORA_SS, LORA_RST, LORA_DI0);
      break;
  }

  current_spi = which;
}

void setup() {
  lcd.setBacklight(HIGH);
  lcd.begin(16, 2);
  
  Serial.begin(115200);   // Initialise serial port
  
  mySerial.begin(9600, SERIAL_8N1, 16, 17);
  mp3.begin();
  uint16_t volume = mp3.getVolume();
  Serial.print("volume was ");
  Serial.println(volume);
  mp3.setVolume(30);
  volume = mp3.getVolume();
  Serial.print(" and changed to  ");
  Serial.println(volume);

  keypad.addEventListener(keypadEvent); // Add an event listener for this keypad

  while (!Serial);    // Do nothing if no serial port is opened (added for Arduinos based on ATMEGA32U4)
 
  xTaskCreatePinnedToCore(mostraLinha, "loop2", 8192, NULL, 1, NULL, 0);//Cria a tarefa "loop2()" com prioridade 1, atribuída ao core 0
  delay(1);
}

void loop() {
  LORA_receive();
  bool card_present = RFID_check();
  if (card_present) {
    LORA_send();
  } else   {
      unsigned long inicio = millis();
      while ((millis() - inicio) < tempoEspera1) {
       char key = keypad.getKey();

        if (key) {
          if ((key != '#') && (key != '*')) {
          mp3.playFolderTrack(1, 6);
          }
          Serial.println(key);
        }
      }
    }  
}



int RFID_check() {
  spi_select(0);
  // Look for new cards
  if ( ! mfrc522.PICC_IsNewCardPresent()) {
    delay(100);
    return false;
  }

  // Select one of the cards
  if ( ! mfrc522.PICC_ReadCardSerial()) {
    return false;
  }

  leituraDados();

  // instrui o PICC quando no estado ACTIVE a ir para um estado de "parada"
  mfrc522.PICC_HaltA();
  // "stop" a encriptação do PCD, deve ser chamado após a comunicação com autenticação, caso contrário novas comunicações não poderão ser iniciadas
  mfrc522.PCD_StopCrypto1();

  return true;
}

void LORA_send() {
  spi_select(1);

  Serial.println("LoRa Sender Test");

  if (!LoRa.begin(LORA_BAND)) {
    Serial.println("Starting LoRa failed!");
    while (1);
  }

  Serial.println("init ok");
   delay(1500);
 // send packet
  sendMessage(str);

  Serial.println("Sent UID");
 
  //delay(2000);
}
void LORA_receive() {
  spi_select(1);

  Serial.println("LoRa Receiver Test");

  if (!LoRa.begin(LORA_BAND)) {
    Serial.println("Starting LoRa failed!");
    while (1);
  }

  Serial.println("init ok");
  LoRa.receive(); 
   delay(1500);
 // send packet
  onReceive(LoRa.parsePacket());

  Serial.println("Receive ok");
 
  //delay(2000);
}
void sendMessage(String outgoing){
  // send packet
  Serial.println("Outgoing");
  Serial.println(outgoing);
  LoRa.beginPacket();                   // start packet
  LoRa.write(destination);              // add destination address
  LoRa.write(localAddress);             // add sender address
  LoRa.write(msgCount);                 // add message ID
  LoRa.write(outgoing.length());        // add payload length
  LoRa.print(outgoing);                 // add payload
  LoRa.endPacket();                     // finish packet and send it
  msgCount++; 
  Serial.println("Mensagem Enviada");
  delay(2000);                       // wait for a second
}

void onReceive(int packetSize)
{
  if (packetSize == 0) return;          // if there's no packet, return

  // read packet header bytes:
  int recipient = LoRa.read();          // recipient address
  byte sender = LoRa.read();            // sender address
  byte incomingMsgId = LoRa.read();     // incoming msg ID
  byte incomingLength = LoRa.read();    // incoming msg length

  String incoming = "";

  while (LoRa.available())
  {
    incoming += (char)LoRa.read();
  }

  if (incomingLength != incoming.length())
  {   // check length for error
    Serial.println("error: message length does not match length");
    return;                             // skip rest of function
  }

  // if the recipient isn't this device or broadcast,
  if (recipient != localAddress && recipient != 0xFF) {
    Serial.println("This message is not for me.");
    return;                             // skip rest of function
  }
   // if message is for this device, or broadcast, print details:
  Serial.println("Received from: 0x" + String(sender, HEX));
  Serial.println("Sent to: 0x" + String(recipient, HEX));
  Serial.println("Message ID: " + String(incomingMsgId));
  Serial.println("Message length: " + String(incomingLength));
  Serial.println("Message: " + incoming);
  Serial.println("RSSI: " + String(LoRa.packetRssi()));
  Serial.println("Snr: " + String(LoRa.packetSnr()));
  Serial.println();
  // verificando se a linha foi requisitada
 
  Serial.println("Remetente ");
  Serial.println(sender);
  idLinha = incoming.toInt();
  Serial.println(idLinha);
  
  for (int i=0; i<=3;i++){
    if (idLinha == requisitadas[i]) {
      for (int j =0; j<3; j++){
        if (sender == vizinhos[j]){
          mp3.playFolderTrack(2, idLinha);
          delay(5000);
          mp3.playFolderTrack(4,(j+1));
        
          return;
        }
      } 
    }
  }  
}

void leituraDados()
{
  str = "";
  //imprime os detalhes tecnicos do cartão/tag
  mfrc522.PICC_DumpDetailsToSerial(&(mfrc522.uid));

  //Prepara a chave - todas as chaves estão configuradas para FFFFFFFFFFFFh (Padrão de fábrica).
  for (byte i = 0; i < 6; i++) key.keyByte[i] = 0xFF;

  //buffer para colocar os dados lidos
  byte buffer[SIZE_BUFFER] = {0};
  //bloco que faremos a operação
  byte bloco = 1;
  byte tamanho = SIZE_BUFFER;


  //faz a autenticação do bloco que vamos operar
  status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, bloco, &key, &(mfrc522.uid)); //line 834 of MFRC522.cpp file
  if (status != MFRC522::STATUS_OK) {
    Serial.print(F("Authentication failed: "));
    Serial.println(mfrc522.GetStatusCodeName(status));
    return;
  }

  //faz a leitura dos dados do bloco
  status = mfrc522.MIFARE_Read(bloco, buffer, &tamanho);
  if (status != MFRC522::STATUS_OK) {
    Serial.print(F("Reading failed: "));
    Serial.println(mfrc522.GetStatusCodeName(status));
    return;
  }

  Serial.print(F("\nDados bloco ["));
  Serial.print(bloco); Serial.print(F("]: "));

  //imprime os dados lidos
  //for (uint8_t i = 0; i < MAX_SIZE_BLOCK; i++)
  for (byte i = 0; i < 3 ; i++)
  {
    //Serial.write(buffer[i]);
    Serial.print(char(buffer[i]));
    str += (char(buffer[i]));

  }
  idLinha = str.toInt();
  for (int i=0; i<=3;i++){
    if (idLinha == requisitadas[i]) {
      mp3.playFolderTrack(1, 12);
      delay(3000);
      mp3.playFolderTrack(2, idLinha);
      delay(5000);
      mp3.playFolderTrack(1,8);
      requisitadas[i]=0;
    }
  }  
  Serial.println("Int linha para verificaçao");
  Serial.println(idLinha);

  
  
}

void keypadEvent(KeypadEvent key) {
  switch (keypad.getState()) {
    case PRESSED:

      if (key == '0') {
        mp3.playFolderTrack(3, 1);

      }
      if (key == '1') {
        mp3.playFolderTrack(3, 2);

      }
      if (key == '2') {
        mp3.playFolderTrack(3, 3);

      }
      if (key == '3') {
        mp3.playFolderTrack(3, 4);

      }
      if (key == '4') {
        mp3.playFolderTrack(3, 5);

      }
      if (key == '5') {
        mp3.playFolderTrack(3, 6);

      }
      if (key == '6') {
        mp3.playFolderTrack(3, 7);

      }
      if (key == '7') {
        mp3.playFolderTrack(3, 8);

      }
      if (key == '8') {
        mp3.playFolderTrack(3, 9);

      }
      if (key == '9') {
        mp3.playFolderTrack(3, 10);

      }

      if (key == '#') {
        mp3.playFolderTrack(1, 1);
        Serial.println("Aviso 1");
      }

      if (key == '*') {
        mp3.playFolderTrack(1, 2);
        Serial.println("Aviso 2");
      }
      break;


    case HOLD:
      if (key == '*') {
        mp3.playFolderTrack(1, 5);
        bool escolha = false;
        Serial.println("Digite a linha");
        unsigned long inicio = millis();
        while ((millis() - inicio) < tempoEspera2) {
          char key = keypad.getKey();
          if (key) {
            Serial.print(key);
            key_linhas += key;
            delay(500);

          }
        }
        Serial.println("linha requisitada: " + key_linhas);
        requer = key_linhas.toInt();
        key_linhas = "";
        for (int i = 0; i <= 3; i++) {
          if (requer == linhas[i]) {
            escolha = true;
          }
        }
        if (escolha) {
          for (int i = 0; i <= 3; i++) {
            if (requer == requisitadas[i]) {
              mp3.playFolderTrack(1, 4);
              Serial.println("Linha já requisitada");
              return;
            }

          }
          mp3.playFolderTrack(1, 7);
          delay(1500);
          mp3.playFolderTrack(2, requer);
          Serial.println("Linha digitada requerida");
          if (num <= 3) {
            requisitadas[num] = requer;
            num++;
          }
          if (num == 4) num = 0;

        } else {
          mp3.playFolderTrack(1, 3);
          Serial.println("Linha não passa nesse ponto");
        }



        break;
      }
  }
}
void mostraLinha(void*z){
  while (1)//Pisca o led infinitamente
  {
    lcd.clear();
    lcd.setCursor(2, 0);
    lcd.print("Para iniciar");
    lcd.setCursor(4, 1);
    lcd.print("Tecle #");
    delay(5000);
    for(int i = 0; i <=3; i++){
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("PARADA ACIONADA");
      lcd.setCursor(3, 1);
      lcd.print("LINHA ");  
      lcd.setCursor(10,1);
      lcd.print(requisitadas[i]);
      delay(5000);
    } 
  }

}