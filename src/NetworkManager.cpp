#include "NetworkManager.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

namespace {
static constexpr const char* AP_SSID = "MyMicroAI-Setup";
static constexpr uint16_t DNS_PORT = 53;
static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;
static constexpr uint32_t PROVIDER_TIMEOUT_MS = 20000;

String normalizeBaseUrl(String url) {
    url.trim();
    while (url.endsWith("/")) {
        url.remove(url.length() - 1);
    }
    return url;
}

String jsonEscape(const String& value) {
    String escaped;
    escaped.reserve(value.length() + 8);
    for (size_t index = 0; index < value.length(); ++index) {
        const char current = value.charAt(index);
        switch (current) {
        case '\\':
            escaped += F("\\\\");
            break;
        case '"':
            escaped += F("\\\"");
            break;
        case '\n':
            escaped += F("\\n");
            break;
        case '\r':
            escaped += F("\\r");
            break;
        case '\t':
            escaped += F("\\t");
            break;
        default:
            escaped += current;
            break;
        }
    }
    return escaped;
}

void appendModelName(String& response, const String& modelName, bool& firstModel) {
    if (modelName.length() == 0) {
        return;
    }

    if (!firstModel) {
        response += ',';
    }
    response += '"';
    response += jsonEscape(modelName);
    response += '"';
    firstModel = false;
}
}

bool NetworkManager::beginStation(const AppConfig& config, uint32_t timeoutMs) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(config.wifiSsid.c_str(), config.wifiPassword.c_str());

    const uint32_t startedAt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startedAt < timeoutMs) {
        delay(250);
        Serial.print('.');
    }
    Serial.println();

    if (WiFi.status() != WL_CONNECTED) {
        lastError_ = "WiFi station connection timeout";
        WiFi.disconnect(true);
        return false;
    }

    lastError_ = "";
    return true;
}

void NetworkManager::beginConfigPortal(ConfigManager& configManager, StorageManager& storage) {
    configManager_ = &configManager;
    storage_ = &storage;
    wifiVerified_ = false;
    providerVerified_ = false;
    rebootPending_ = false;
    pendingConfig_ = AppConfig();

    WiFi.mode(WIFI_AP_STA);
    WiFi.disconnect(true, true);
    delay(100);
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
    if (!WiFi.softAP(AP_SSID)) {
        lastError_ = "Cannot start open SoftAP";
        return;
    }

    dnsServer_.start(DNS_PORT, "*", WiFi.softAPIP());

    server_.on("/", HTTP_GET, [this]() { handleRoot(); });
    server_.on("/api/wifi-setup", HTTP_POST, [this]() { handleWifiSetup(); });
    server_.on("/api/models", HTTP_POST, [this]() { handleModelScan(); });
    server_.on("/api/final-save", HTTP_POST, [this]() { handleFinalSave(); });
    server_.on("/generate_204", HTTP_GET, [this]() { handleCaptivePortalProbe(); });
    server_.on("/gen_204", HTTP_GET, [this]() { handleCaptivePortalProbe(); });
    server_.on("/hotspot-detect.html", HTTP_GET, [this]() { handleCaptivePortalProbe(); });
    server_.on("/library/test/success.html", HTTP_GET, [this]() { handleCaptivePortalProbe(); });
    server_.on("/ncsi.txt", HTTP_GET, [this]() { handleCaptivePortalProbe(); });
    server_.on("/connecttest.txt", HTTP_GET, [this]() { handleCaptivePortalProbe(); });
    server_.onNotFound([this]() { redirectToPortal(); });
    server_.begin();
    lastError_ = "";
}

void NetworkManager::handleClient() {
    dnsServer_.processNextRequest();
    server_.handleClient();

    if (rebootPending_ && millis() >= rebootAtMs_) {
        delay(100);
        ESP.restart();
    }
}

bool NetworkManager::isStationConnected() const {
    return WiFi.status() == WL_CONNECTED;
}

String NetworkManager::localAddress() const {
    if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) {
        return WiFi.softAPIP().toString();
    }
    return WiFi.localIP().toString();
}

const char* NetworkManager::lastError() const {
    return lastError_.c_str();
}

void NetworkManager::sendWizardPage() {
    static const char page[] PROGMEM = R"HTML(
<!doctype html><html lang="ru"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1"><title>MyMicroAI Setup</title><style>
:root{color-scheme:dark;--bg:#0A0A0C;--card:#050506;--field:#030304;--line:#1F1F24;--line2:#2A2A31;--text:#FFFFFF;--muted:#7E7E8F;--accent:#FF5A00;--bad:#ff6b4a}*{box-sizing:border-box}html,body{margin:0;min-height:100%;background:var(--bg);color:var(--text);font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Inter,Arial,sans-serif}body{display:grid;place-items:center;padding:22px}.wrap{width:min(760px,100%)}.card{border:1px solid var(--line);border-radius:18px;background:linear-gradient(180deg,#080809,#030304);box-shadow:0 24px 80px rgba(0,0,0,.48);padding:28px}.brand{display:flex;align-items:center;gap:14px;margin-bottom:22px}.mark{width:34px;height:34px;border:1px solid var(--line2);border-radius:10px;display:grid;place-items:center;color:var(--accent);font-weight:900}.kicker{margin:0 0 6px;color:var(--accent);font-size:12px;font-weight:800;letter-spacing:.18em;text-transform:uppercase}.title{margin:0;font-size:clamp(30px,6vw,46px);line-height:1;letter-spacing:-.05em}.sub{margin:14px 0 0;color:var(--muted);line-height:1.6}.steps{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;margin:26px 0}.stepdot{border:1px solid var(--line);border-radius:12px;padding:11px 12px;color:var(--muted);background:#060607;font-weight:700;font-size:13px}.stepdot.active{border-color:var(--accent);color:var(--text)}.panel{display:none;opacity:0;transform:translateY(6px);transition:opacity .22s ease,transform .22s ease}.panel.active{display:block;opacity:1;transform:none}.grid{display:grid;grid-template-columns:1fr 1fr;gap:14px}.field{margin:0}.field.full{grid-column:1/-1}label{display:block;margin:0 0 7px;color:var(--muted);font-size:13px;font-weight:700}input,select{width:100%;height:46px;border:1px solid var(--line);border-radius:10px;background:var(--field);color:var(--text);padding:0 13px;font:inherit;outline:none;transition:border-color .18s ease,box-shadow .18s ease}input:focus,select:focus{border-color:var(--accent);box-shadow:0 0 0 3px rgba(255,90,0,.13)}button{height:48px;border:0;border-radius:10px;background:var(--accent);color:#090909;font:inherit;font-weight:850;cursor:pointer;transition:transform .16s ease,filter .16s ease,opacity .16s ease}button:hover{filter:brightness(1.08);transform:translateY(-1px)}button:disabled{opacity:.55;cursor:not-allowed;transform:none}.primary{width:100%;margin-top:18px}.modelrow{display:grid;grid-template-columns:1fr 54px;gap:10px}.ghost{background:#080809;color:var(--text);border:1px solid var(--line)}.chips{display:flex;gap:8px;flex-wrap:wrap;margin-bottom:16px}.chip{border:1px solid var(--line);border-radius:999px;color:var(--muted);padding:7px 10px;font-size:12px}.msg{min-height:22px;margin-top:13px;color:var(--muted)}.msg.ok{color:var(--accent)}.msg.err{color:var(--bad)}.msg.warn{color:var(--text)}.loader{display:none;width:20px;height:20px;border:2px solid var(--line2);border-top-color:var(--accent);border-radius:50%;animation:spin .8s linear infinite;margin-top:14px}.loader.show{display:block}@keyframes spin{to{transform:rotate(360deg)}}@media(max-width:680px){body{padding:12px}.card{padding:20px;border-radius:16px}.grid{grid-template-columns:1fr}.steps{grid-template-columns:1fr}.title{font-size:34px}}
</style></head><body><main class="wrap"><section class="card"><div class="brand"><div class="mark">M</div><div><p class="kicker">MyMicroAI Setup</p><h1 class="title">Minimal AI handheld</h1></div></div><p class="sub">Подключите Wi-Fi, проверьте AI provider и сохраните конфиг. Интерфейс работает локально на ESP32.</p><div class="steps"><div id="dot1" class="stepdot active">1 · Wi-Fi</div><div id="dot2" class="stepdot">2 · API</div><div id="dot3" class="stepdot">3 · Save</div></div>
<div id="step1" class="panel active"><div class="chips"><span class="chip">AP: MyMicroAI-Setup</span><span class="chip">open network</span></div><div class="grid"><p class="field"><label>SSID</label><input id="ssid" placeholder="Wi-Fi network" autocomplete="off"></p><p class="field"><label>Password</label><input id="wifiPass" type="password" placeholder="Wi-Fi password"></p></div><button id="wifiBtn" class="primary" onclick="setupWifi()">Continue</button><div id="wifiLoad" class="loader"></div><div id="wifiMsg" class="msg"></div></div>
<div id="step2" class="panel"><div class="grid"><p class="field"><label>API Key</label><input id="apiKey" type="password" placeholder="sk-..." autocomplete="off"></p><p class="field"><label>Base URL</label><input id="baseUrl" value="https://api.openai.com/v1" placeholder="https://api.openai.com/v1"></p><p class="field full"><label>Model</label><span class="modelrow"><select id="model" disabled><option>Load models first</option></select><button id="modelsBtn" class="ghost" onclick="loadModels()" title="Load models">↻</button></span></p></div><button id="saveBtn" class="primary" onclick="finalSave()">Save and start Work Mode</button><div id="modelsLoad" class="loader"></div><div id="modelsMsg" class="msg"></div></div>
<div id="step3" class="panel"><p class="sub">Готово. Конфиг сохранён на SD-карту, устройство перезагружается в Work Mode.</p><div id="saveLoad" class="loader show"></div><div id="saveMsg" class="msg ok">Restarting...</div></div></section></main><script>
let wifi={ssid:'',password:''};let provider={apiKey:'',baseUrl:'',model:''};
function $(id){return document.getElementById(id)}
function msg(id,text,kind){const el=$(id);el.className='msg '+(kind||'');el.textContent=text||''}
function load(id,on){$(id).classList.toggle('show',!!on)}
function busy(btn,on){$(btn).disabled=!!on}
function goStep(n){for(let i=1;i<=3;i++){const p=$('step'+i);p.classList.toggle('active',i===n);$('dot'+i).classList.toggle('active',i===n)}}
async function postJson(url,data){const r=await fetch(url,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)});const j=await r.json().catch(()=>({status:'error',message:'Invalid ESP32 response'}));if(!r.ok||j.status==='error')throw new Error(j.message||'Request failed');return j}
async function setupWifi(){const ssid=$('ssid').value.trim(),password=$('wifiPass').value;if(!ssid){msg('wifiMsg','Введите SSID','err');return}wifi={ssid,password};msg('wifiMsg','Connecting to router...','warn');load('wifiLoad',true);busy('wifiBtn',true);try{await postJson('/api/wifi-setup',wifi);msg('wifiMsg','Wi-Fi connected.','ok');setTimeout(()=>goStep(2),260)}catch(e){msg('wifiMsg',e.message,'err')}finally{load('wifiLoad',false);busy('wifiBtn',false)}}
async function loadModels(){const apiKey=$('apiKey').value.trim(),baseUrl=$('baseUrl').value.trim();if(!apiKey||!baseUrl){msg('modelsMsg','Введите API Key и Base URL','err');return}provider.apiKey=apiKey;provider.baseUrl=baseUrl;msg('modelsMsg','Loading models...','warn');load('modelsLoad',true);busy('modelsBtn',true);try{const res=await postJson('/api/models',{api_key:apiKey,base_url:baseUrl});const select=$('model');select.innerHTML='';(res.models||[]).forEach(name=>{const option=document.createElement('option');option.value=name;option.textContent=name;select.appendChild(option)});select.disabled=false;if(!res.models||res.models.length===0)throw new Error('Провайдер не вернул список моделей');msg('modelsMsg','Models loaded: '+res.models.length,'ok')}catch(e){msg('modelsMsg',e.message,'err')}finally{load('modelsLoad',false);busy('modelsBtn',false)}}
async function finalSave(){const model=$('model').value;if(!model||$('model').disabled){msg('modelsMsg','Сначала загрузите модели и выберите модель','err');return}provider.model=model;provider.apiKey=$('apiKey').value.trim();provider.baseUrl=$('baseUrl').value.trim();if(!provider.apiKey||!provider.baseUrl){msg('modelsMsg','Введите API Key и Base URL','err');return}msg('modelsMsg','Saving config...','warn');load('modelsLoad',true);busy('saveBtn',true);busy('modelsBtn',true);try{await postJson('/api/final-save',{ssid:wifi.ssid,password:wifi.password,api_key:provider.apiKey,base_url:provider.baseUrl,model});goStep(3)}catch(e){msg('modelsMsg',e.message,'err');busy('saveBtn',false);busy('modelsBtn',false)}finally{load('modelsLoad',false)}}
</script></body></html>
)HTML";

    server_.send_P(200, "text/html; charset=utf-8", page);
}

void NetworkManager::handleRoot() {
    sendWizardPage();
}

void NetworkManager::handleWifiSetup() {
    JsonDocument request;
    if (!readJsonBody(request)) {
        return;
    }

    const char* ssid = request["ssid"] | "";
    const char* password = request["password"] | "";
    if (strlen(ssid) == 0) {
        sendJsonError(400, "Введите SSID");
        return;
    }

    WiFi.disconnect(false, false);
    delay(150);
    WiFi.begin(ssid, password);

    const uint32_t startedAt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startedAt < WIFI_CONNECT_TIMEOUT_MS) {
        delay(250);
    }

    if (WiFi.status() != WL_CONNECTED) {
        wifiVerified_ = false;
        WiFi.disconnect(false, false);
        sendJsonError(409, "Неверный пароль или сеть не найдена");
        return;
    }

    pendingConfig_.wifiSsid = ssid;
    pendingConfig_.wifiPassword = password;
    wifiVerified_ = true;
    providerVerified_ = false;
    server_.send(200, "application/json", F("{\"status\":\"success\"}"));
}

void NetworkManager::handleModelScan() {
    if (!wifiVerified_ || WiFi.status() != WL_CONNECTED) {
        sendJsonError(409, "Сначала успешно подключитесь к Wi-Fi");
        return;
    }

    JsonDocument request;
    if (!readJsonBody(request)) {
        return;
    }

    const String apiKey = request["api_key"] | "";
    const String baseUrl = normalizeBaseUrl(request["base_url"] | "");
    if (apiKey.length() == 0 || baseUrl.length() == 0) {
        sendJsonError(400, "Введите API Key и Base URL");
        return;
    }

    const String modelsUrl = baseUrl + "/models";
    HTTPClient http;
    WiFiClientSecure secureClient;
    WiFiClient plainClient;
    bool httpStarted = false;

    if (modelsUrl.startsWith("https://")) {
        secureClient.setInsecure();
        httpStarted = http.begin(secureClient, modelsUrl);
    } else {
        httpStarted = http.begin(plainClient, modelsUrl);
    }

    if (!httpStarted) {
        sendJsonError(500, "Не удалось подготовить HTTP запрос к провайдеру");
        return;
    }

    http.setTimeout(PROVIDER_TIMEOUT_MS);
    http.addHeader("Authorization", String("Bearer ") + apiKey);
    http.addHeader("Accept", "application/json");
    const int httpCode = http.GET();
    if (httpCode < 200 || httpCode >= 300) {
        http.end();
        sendJsonError(502, "Провайдер отклонил запрос /models");
        return;
    }

    const String payload = http.getString();
    http.end();

    JsonDocument responseDocument;
    const DeserializationError error = deserializeJson(responseDocument, payload);
    if (error) {
        sendJsonError(502, "Провайдер вернул некорректный JSON");
        return;
    }

    String response;
    response.reserve(2048);
    response += F("{\"status\":\"success\",\"models\":[");
    bool firstModel = true;

    JsonVariant data = responseDocument["data"];
    if (data.is<JsonArray>()) {
        for (JsonVariant item : data.as<JsonArray>()) {
            if (item.is<const char*>()) {
                appendModelName(response, item.as<String>(), firstModel);
            } else {
                appendModelName(response, item["id"].as<String>(), firstModel);
                if (firstModel) {
                    appendModelName(response, item["name"].as<String>(), firstModel);
                }
            }
        }
    } else if (responseDocument.is<JsonArray>()) {
        for (JsonVariant item : responseDocument.as<JsonArray>()) {
            if (item.is<const char*>()) {
                appendModelName(response, item.as<String>(), firstModel);
            } else {
                appendModelName(response, item["id"].as<String>(), firstModel);
                if (firstModel) {
                    appendModelName(response, item["name"].as<String>(), firstModel);
                }
            }
        }
    } else if (responseDocument["models"].is<JsonArray>()) {
        for (JsonVariant item : responseDocument["models"].as<JsonArray>()) {
            if (item.is<const char*>()) {
                appendModelName(response, item.as<String>(), firstModel);
            } else {
                appendModelName(response, item["id"].as<String>(), firstModel);
                if (firstModel) {
                    appendModelName(response, item["name"].as<String>(), firstModel);
                }
            }
        }
    }

    response += F("]}");
    if (firstModel) {
        sendJsonError(502, "В ответе провайдера не найдено моделей");
        return;
    }

    pendingConfig_.apiBaseUrl = baseUrl;
    pendingConfig_.apiKey = apiKey;
    providerVerified_ = true;
    server_.send(200, "application/json", response);
}

void NetworkManager::handleFinalSave() {
    if (!wifiVerified_) {
        sendJsonError(409, "Wi-Fi еще не проверен");
        return;
    }
    if (!providerVerified_) {
        sendJsonError(409, "Сначала загрузите модели провайдера");
        return;
    }
    if (configManager_ == nullptr || storage_ == nullptr) {
        sendJsonError(500, "Config portal is not initialized");
        return;
    }

    JsonDocument request;
    if (!readJsonBody(request)) {
        return;
    }

    const String selectedModel = request["model"] | "";
    if (selectedModel.length() == 0) {
        sendJsonError(400, "Выберите модель");
        return;
    }

    pendingConfig_.wifiSsid = request["ssid"] | pendingConfig_.wifiSsid;
    pendingConfig_.wifiPassword = request["password"] | pendingConfig_.wifiPassword;
    pendingConfig_.apiBaseUrl = normalizeBaseUrl(request["base_url"] | pendingConfig_.apiBaseUrl);
    pendingConfig_.apiKey = request["api_key"] | pendingConfig_.apiKey;
    pendingConfig_.modelName = selectedModel;

    if (!pendingConfig_.isComplete()) {
        sendJsonError(400, "Итоговый конфиг неполный");
        return;
    }

    if (!configManager_->save(*storage_, pendingConfig_)) {
        sendJsonError(500, configManager_->lastError());
        return;
    }

    rebootPending_ = true;
    rebootAtMs_ = millis() + 1400;
    server_.send(200, "application/json", F("{\"status\":\"success\",\"message\":\"saved\"}"));
}

void NetworkManager::handleCaptivePortalProbe() {
    redirectToPortal();
}

void NetworkManager::redirectToPortal() {
    const String url = String("http://") + WiFi.softAPIP().toString() + "/";
    server_.sendHeader("Location", url, true);
    server_.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server_.sendHeader("Pragma", "no-cache");
    server_.sendHeader("Expires", "0");
    server_.send(302, "text/plain", "Redirecting to MyMicroAI setup wizard");
}

bool NetworkManager::readJsonBody(JsonDocument& document) {
    const String body = server_.arg("plain");
    if (body.length() == 0) {
        sendJsonError(400, "JSON body is empty");
        return false;
    }

    const DeserializationError error = deserializeJson(document, body);
    if (error) {
        sendJsonError(400, "Invalid JSON body");
        return false;
    }

    return true;
}

void NetworkManager::sendJsonError(int code, const char* message) {
    String response;
    response.reserve(96 + strlen(message));
    response += F("{\"status\":\"error\",\"message\":\"");
    response += jsonEscape(message);
    response += F("\"}");
    server_.send(code, "application/json", response);
}
