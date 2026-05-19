# Wi-Fi 联网与配置门户

当前工程已加入一个 **常驻 SoftAP + 网页配置** 的联网入口。

## 使用方式

设备启动后会一直开启一个配置 AP：

- SSID：`Skainet-Setup`
- 密码：`skainet8`
- 固定地址：`http://192.168.4.1`

连接这个 AP 后，打开浏览器访问 `192.168.4.1`，填入你要连接的路由器：

- `SSID`
- `密码`

保存后设备会：

1. 把路由器信息写入 NVS
2. 立即尝试用 STA 模式连接该路由器
3. 同时继续保留配置 AP，方便后续重新改网

## 当前行为

- **已保存过 Wi-Fi**：上电自动尝试连接
- **没有保存过 Wi-Fi**：只开配置 AP，等待网页配置
- **路由器连接失败**：不会卡死，仍可通过 AP 页面重新配置

## Demo 阶段免网页首配

如果你只是做 demo，可以给固件写入一组“编译期默认 Wi-Fi”：

- 当 **NVS 里还没有保存 Wi-Fi** 时，设备会先尝试这组默认值
- 如果后面你通过网页改过 Wi-Fi，则 **网页保存值优先**

可在 `sdkconfig.defaults.esp32s3` 中打开下面几行，或通过 `menuconfig` 配置：

```ini
CONFIG_WIFI_PORTAL_USE_DEFAULT_STA=y
CONFIG_WIFI_PORTAL_DEFAULT_STA_SSID="YourWiFi"
CONFIG_WIFI_PORTAL_DEFAULT_STA_PASSWORD="YourPassword"
```

这样刷机后首启就会自动尝试联网，不需要先登录网页。要改网时，再访问 `192.168.4.1` 修改即可。

## 为什么这样做

这个方案比把 SSID/密码硬编码进代码更灵活，尤其适合：

- 经常换路由器
- 现场部署后需要手机改网
- 后面要给 FastAPI 地址做网页配置扩展

## 后续更好的办法

如果后面你要做得更正式，优先级更高的是：

1. **ESP-IDF Provisioning Manager**
   - 官方方案
   - 可做 BLE 配网 / SoftAP 配网
   - 更适合产品化
2. **配置门户再扩展一页**
   - 除了 Wi-Fi，再配置 FastAPI 地址、端口、设备名
3. **加入局域网发现**
   - 例如 mDNS，让你不必记 IP

当前这版更适合先把联网和后续云端链路跑通。
