#include "pch.h"
#include "constants.h"
#include "WifiManager.h"
#include "TimeManager.h"

WifiManager::WifiManager()
{
    lastReconnectAttempt = 0;
    retryCount = 0;
}

// ---------------------------------------------------------------------------
// Inicialização do WiFi
// ---------------------------------------------------------------------------

int WifiManager::setupWifi(const char* contextName, const char* ssid, const char* password)
{
    // 🔒 Salva credenciais internamente
    savedSSID = ssid;
    savedPassword = password;

    logDebugf("%s: Estabelecendo conexão inicial", contextName);

    WiFi.mode(WIFI_STA);
    WiFi.begin(savedSSID.c_str(), savedPassword.c_str());


    while (WiFi.status() != WL_CONNECTED)
    {
        for (int i = 0; i < 5; i++)
        {
            digitalWrite(LED2, i % 2);
            delay(200);
        }
        logDebug(".");
    }

    logDebugf("\n%s: Contectado com sucesso \n", contextName);
    logDebugf("%s: IP address = %s \n",
              contextName,
              WiFi.localIP().toString().c_str());

    return 1;
}

// ---------------------------------------------------------------------------
// Monitoramento de reconexão WiFi
// ---------------------------------------------------------------------------

void WifiManager::checkWifiReconnection()
{
    const unsigned long reconnectInterval = 60000; // 60 segundos
    unsigned long now = millis();
    wl_status_t status = WiFi.status();

    // Se está conectado, zera contadores e retorna
    if (status == WL_CONNECTED)
    {
        retryCount = 0;
        return;
    }

    // Respeita intervalo mínimo entre tentativas
    if (now - lastReconnectAttempt < reconnectInterval)
        return;

    // Log no arquivo
    logIt(TimeManager::getFormatted(FMT_FULL), true);
    logIt(": lp-wf-rcnt\n", true);

    // Tenta reconectar usando as credenciais salvas
    WiFi.disconnect(false);
    WiFi.mode(WIFI_STA);
    WiFi.begin(savedSSID.c_str(), savedPassword.c_str());
    delay(3000);

    lastReconnectAttempt = now;
    retryCount++;

    logIt((String("🔁 WiFi retry #") + retryCount).c_str(), true);
}
