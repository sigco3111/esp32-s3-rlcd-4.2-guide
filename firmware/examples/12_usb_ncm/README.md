# 12 — USB CDC-NCM (이더넷 NIC)

USB 를 **이더넷 NIC** 로 노출한다. Mac 은 이 인터페이스를
일반 유선이더넷처럼 인식한다.

## 왜 필요한가

이 보드가 붙어 있는 네트워크에는 **2.4GHz Wi-Fi 가 없다.**
ESP32-S3 는 2.4GHz 전용이라 STA 로 붙을 수 없다.

CDC-NCM 은 이 제약 자체를 없앤다 —
**보드가 이더넷 장비가 되므로** IP 나 DNS 같은 Wi-Fi 개념이 통째로 사라진다.

```
[Mac] --USB(CDC-NCM, L2 이더넷 프레임)--> [ESP32-S3 lwIP netif 'usb0']
                                              └─ HTTP 서버(:80)
```

Mac 의 인터넷 공유(NAT)를 빌리면 보드도 외부 통신이 가능하다.

## ESP-IDF 5.5 lwIP 주의 (실제 겪은 함정)

```c
netif_output_fn(netif, pbuf, ipaddr)          // 3인자
tcpip_input(pbuf, netif)                      // 이 순서
ip4_addr_t                                     // IP 타입
IP4ADDR_STRLEN_MAX                             // IP 문자열 길이
```

★ `esp_netif` 공개 API로는 외부 netif 를 붙일 수 없어
**lwIP 를 직접 다뤄야 한다.** ESP-IDF 5.5 에서 시그니처가 바뀌었다.

→ [문서: lwIP API 미스매치](../docs/pitfalls.md#17-lwip-api-미스매치)

## 접속

보드가 `192.168.7.2` 로 뜨고, `http://192.168.7.2` 로 HTTP 서버에 접근한다.

## 빌드

```bash
cd 12_usb_ncm

# ★ sdkconfig 는 생성물이라 커밋하지 않는다.
#   처음 빌드할 때 target 과 flash size 를 명시해야 한다.
idf.py set-target esp32s3
idf.py build
```

★ **왜 `set-target` 이 필요한가**
`sdkconfig.defaults` 에 `CONFIG_IDF_TARGET="esp32s3"` 가 있어도
IDF 는 처음에 `esp32` 로 target 을 잡는다. 그대로 빌드하면
`is not compatible with target "esp32"` 로 막힌다.

★ **왜 flash size 를 명시해야 하는가**
ESP-IDF 의 기본 flash size 는 **2MB** 다. 이 보드는 **16MB** 이고
`partitions.csv` 는 3.1MB 를 쓴다. 기본값으로 빌드하면
`Partitions tables occupies 3.1MB ... does not fit in configured flash size 2MB`
로 막힌다.

전 예제 한 번에 빌드하려면 저장소 루트에서:
```bash
./firmware/build-all-examples.sh
```
