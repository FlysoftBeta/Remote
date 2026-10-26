# 07 Widevine CDM（捆绑，用户要求；暂不考虑许可）

依据：`research/m1/widevine-steps.md`（Codex 任务，含来源核对与校验和）、pin 上 `third_party/widevine/cdm/widevine.gni`。

1. 获取：与 Chromium 组件更新器/Brave 相同的来源——向 `https://update.googleapis.com/service/update2/json` 请求组件
   `oimompecagnajdejgnnjijobebaeigek`（os=win, arch=x64, prodversion=157），下载返回的 `.crx3` 并校验响应中的 SHA-256；去掉 CRX3 头即 zip。
   当前版本 **4.10.3112.0**（CDM module 4 / interface 10 / host 10-11，与 pin 匹配），暂存于 `C:\src\tmp\m1\widevine\`。
2. 放置：`manifest.json`、`widevinecdm.dll`、`widevinecdm.dll.sig` 平铺复制到 `src/third_party/widevine/cdm/win/x64/`（不入补丁、不入库；
   新 checkout 需重复本步，后续可做成 `cr widevine` 子命令）。
3. GN 参数（dev 与 rel）：`enable_widevine = true`、`enable_library_cdms = true`、`bundle_widevine_cdm = true`、`enable_cdm_host_verification = false`。
   构建后 CDM 位于 `out\<cfg>\WidevineCdm\_platform_specific\win_x64\`（发布时整目录随 chrome.exe 分发）。
4. 验证：`chrome://components` 不列出捆绑的 CDM；用页面 `navigator.requestMediaKeySystemAccess('com.widevine.alpha', [{videoCapabilities:[{contentType:'video/mp4; codecs="avc1.42E01E"'}]}])` 与 `chrome://media-internals`。
5. 限制：我们的 chrome.exe 没有 Google VMP 签名，部分许可证服务器（尤其高安全级别/某些流媒体服务）会拒绝发放许可证；L3 软件 CDM。
