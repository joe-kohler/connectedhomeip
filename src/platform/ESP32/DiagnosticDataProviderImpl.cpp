/*
 *
 *    Copyright (c) 2021 Project CHIP Authors
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

/**
 *    @file
 *          Provides an implementation of the DiagnosticDataProvider object
 *          for ESP32 platform.
 */

#include <platform/internal/CHIPDeviceLayerInternal.h>

#include <app-common/zap-generated/enums.h>
#include <crypto/CHIPCryptoPAL.h>
#include <platform/DiagnosticDataProvider.h>
#include <platform/ESP32/DiagnosticDataProviderImpl.h>
#include <platform/ESP32/ESP32Utils.h>

#include "esp_event.h"
#include "esp_heap_caps_init.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_spi_flash.h"
#include "esp_system.h"
#include "esp_wifi.h"

using namespace ::chip;
using namespace ::chip::TLV;
using namespace ::chip::DeviceLayer;
using namespace ::chip::DeviceLayer::Internal;
using namespace ::chip::app::Clusters::GeneralDiagnostics;

namespace {

InterfaceType GetInterfaceType(const char * if_desc)
{
    if (strncmp(if_desc, "ap", strnlen(if_desc, 2)) == 0 || strncmp(if_desc, "sta", strnlen(if_desc, 3)) == 0)
        return InterfaceType::EMBER_ZCL_INTERFACE_TYPE_WI_FI;
    if (strncmp(if_desc, "openthread", strnlen(if_desc, 10)) == 0)
        return InterfaceType::EMBER_ZCL_INTERFACE_TYPE_THREAD;
    if (strncmp(if_desc, "eth", strnlen(if_desc, 3)) == 0)
        return InterfaceType::EMBER_ZCL_INTERFACE_TYPE_ETHERNET;
    return InterfaceType::EMBER_ZCL_INTERFACE_TYPE_UNSPECIFIED;
}

#if CHIP_DEVICE_CONFIG_ENABLE_WIFI
uint8_t MapAuthModeToSecurityType(wifi_auth_mode_t authmode)
{
    switch (authmode)
    {
    case WIFI_AUTH_OPEN:
        return 1;
    case WIFI_AUTH_WEP:
        return 2;
    case WIFI_AUTH_WPA_PSK:
        return 3;
    case WIFI_AUTH_WPA2_PSK:
        return 4;
    case WIFI_AUTH_WPA3_PSK:
        return 5;
    default:
        return 0;
    }
}

uint8_t GetWiFiVersionFromAPRecord(wifi_ap_record_t ap_info)
{
    if (ap_info.phy_11n)
        return 3;
    else if (ap_info.phy_11g)
        return 2;
    else if (ap_info.phy_11b)
        return 1;
    else
        return 0;
}
#endif // CHIP_DEVICE_CONFIG_ENABLE_WIFI

} // namespace

namespace chip {
namespace DeviceLayer {

DiagnosticDataProviderImpl & DiagnosticDataProviderImpl::GetDefaultInstance()
{
    static DiagnosticDataProviderImpl sInstance;
    return sInstance;
}

CHIP_ERROR DiagnosticDataProviderImpl::GetCurrentHeapFree(uint64_t & currentHeapFree)
{
    currentHeapFree = esp_get_free_heap_size();
    return CHIP_NO_ERROR;
}

CHIP_ERROR DiagnosticDataProviderImpl::GetCurrentHeapUsed(uint64_t & currentHeapUsed)
{
    currentHeapUsed = heap_caps_get_total_size(MALLOC_CAP_DEFAULT) - esp_get_free_heap_size();
    return CHIP_NO_ERROR;
}

CHIP_ERROR DiagnosticDataProviderImpl::GetCurrentHeapHighWatermark(uint64_t & currentHeapHighWatermark)
{
    currentHeapHighWatermark = heap_caps_get_total_size(MALLOC_CAP_DEFAULT) - esp_get_minimum_free_heap_size();
    return CHIP_NO_ERROR;
}

CHIP_ERROR DiagnosticDataProviderImpl::GetRebootCount(uint16_t & rebootCount)
{
    uint32_t count = 0;

    CHIP_ERROR err = ConfigurationMgr().GetRebootCount(count);

    if (err == CHIP_NO_ERROR)
    {
        VerifyOrReturnError(count <= UINT16_MAX, CHIP_ERROR_INVALID_INTEGER_VALUE);
        rebootCount = static_cast<uint16_t>(count);
    }

    return err;
}

CHIP_ERROR DiagnosticDataProviderImpl::GetUpTime(uint64_t & upTime)
{
    System::Clock::Timestamp currentTime = System::SystemClock().GetMonotonicTimestamp();
    System::Clock::Timestamp startTime   = PlatformMgrImpl().GetStartTime();

    if (currentTime >= startTime)
    {
        upTime = std::chrono::duration_cast<System::Clock::Seconds64>(currentTime - startTime).count();
        return CHIP_NO_ERROR;
    }

    return CHIP_ERROR_INVALID_TIME;
}

CHIP_ERROR DiagnosticDataProviderImpl::GetTotalOperationalHours(uint32_t & totalOperationalHours)
{
    uint64_t upTime = 0;

    if (GetUpTime(upTime) == CHIP_NO_ERROR)
    {
        uint32_t totalHours = 0;
        if (ConfigurationMgr().GetTotalOperationalHours(totalHours) == CHIP_NO_ERROR)
        {
            VerifyOrReturnError(upTime / 3600 <= UINT32_MAX, CHIP_ERROR_INVALID_INTEGER_VALUE);
            totalOperationalHours = totalHours + static_cast<uint32_t>(upTime / 3600);
            return CHIP_NO_ERROR;
        }
    }

    return CHIP_ERROR_INVALID_TIME;
}

CHIP_ERROR DiagnosticDataProviderImpl::GetBootReason(uint8_t & bootReason)
{
    bootReason = BootReasonType::Unspecified;
    uint8_t reason;
    reason = static_cast<uint8_t>(esp_reset_reason());
    if (reason == ESP_RST_UNKNOWN)
    {
        bootReason = BootReasonType::Unspecified;
    }
    else if (reason == ESP_RST_POWERON)
    {
        bootReason = BootReasonType::PowerOnReboot;
    }
    else if (reason == ESP_RST_BROWNOUT)
    {
        bootReason = BootReasonType::BrownOutReset;
    }
    else if (reason == ESP_RST_SW)
    {
        bootReason = BootReasonType::SoftwareReset;
    }
    else if (reason == ESP_RST_INT_WDT)
    {
        bootReason = BootReasonType::SoftwareWatchdogReset;
        /* Reboot can be due to hardware or software watchdog*/
    }
    return CHIP_NO_ERROR;
}

CHIP_ERROR DiagnosticDataProviderImpl::GetNetworkInterfaces(NetworkInterface ** netifpp)
{
    esp_netif_t * netif     = esp_netif_next(NULL);
    NetworkInterface * head = NULL;
    if (netif == NULL)
    {
        ChipLogError(DeviceLayer, "Failed to get network interfaces");
    }
    else
    {
        for (esp_netif_t * ifa = netif; ifa != NULL; ifa = esp_netif_next(ifa))
        {
            NetworkInterface * ifp = new NetworkInterface();
            strncpy(ifp->Name, esp_netif_get_ifkey(ifa), Inet::InterfaceId::kMaxIfNameLength);
            ifp->Name[Inet::InterfaceId::kMaxIfNameLength - 1] = '\0';
            ifp->name                                          = CharSpan(ifp->Name, strlen(ifp->Name));
            ifp->fabricConnected                               = true;
            ifp->type                                          = GetInterfaceType(esp_netif_get_desc(ifa));
            ifp->offPremiseServicesReachableIPv4               = false;
            ifp->offPremiseServicesReachableIPv6               = false;
            if (esp_netif_get_mac(ifa, ifp->MacAddress) != ESP_OK)
            {
                ChipLogError(DeviceLayer, "Failed to get network hardware address");
            }
            else
            {
                ifp->hardwareAddress = ByteSpan(ifp->MacAddress, 6);
            }
            ifp->Next = head;
            head      = ifp;
        }
    }
    *netifpp = head;
    return CHIP_NO_ERROR;
}

void DiagnosticDataProviderImpl::ReleaseNetworkInterfaces(NetworkInterface * netifp)
{
    while (netifp)
    {
        NetworkInterface * del = netifp;
        netifp                 = netifp->Next;
        delete del;
    }
}

#if CHIP_DEVICE_CONFIG_ENABLE_WIFI
CHIP_ERROR DiagnosticDataProviderImpl::GetWiFiBssId(ByteSpan & BssId)
{
    wifi_ap_record_t ap_info;
    esp_err_t err;
    uint8_t macAddress[kMaxHardwareAddrSize];

    err = esp_wifi_sta_get_ap_info(&ap_info);
    if (err == ESP_OK)
    {
        memcpy(macAddress, ap_info.bssid, 6);
    }
    BssId = ByteSpan(macAddress, 6);
    return CHIP_NO_ERROR;
}

CHIP_ERROR DiagnosticDataProviderImpl::GetWiFiSecurityType(uint8_t & securityType)
{
    securityType = 0;
    wifi_ap_record_t ap_info;
    esp_err_t err;

    err = esp_wifi_sta_get_ap_info(&ap_info);
    if (err == ESP_OK)
    {
        securityType = MapAuthModeToSecurityType(ap_info.authmode);
    }
    return CHIP_NO_ERROR;
}

CHIP_ERROR DiagnosticDataProviderImpl::GetWiFiVersion(uint8_t & wifiVersion)
{
    wifiVersion = 0;
    wifi_ap_record_t ap_info;
    esp_err_t err;
    err = esp_wifi_sta_get_ap_info(&ap_info);
    if (err == ESP_OK)
    {
        wifiVersion = GetWiFiVersionFromAPRecord(ap_info);
    }
    return CHIP_NO_ERROR;
}

CHIP_ERROR DiagnosticDataProviderImpl::GetWiFiChannelNumber(uint16_t & channelNumber)
{
    channelNumber = 0;
    wifi_ap_record_t ap_info;
    esp_err_t err;

    err = esp_wifi_sta_get_ap_info(&ap_info);
    if (err == ESP_OK)
    {
        channelNumber = ap_info.primary;
    }
    return CHIP_NO_ERROR;
}

CHIP_ERROR DiagnosticDataProviderImpl::GetWiFiRssi(int8_t & rssi)
{
    rssi = 0;
    wifi_ap_record_t ap_info;
    esp_err_t err;

    err = esp_wifi_sta_get_ap_info(&ap_info);

    if (err == ESP_OK)
    {
        rssi = ap_info.rssi;
    }
    return CHIP_NO_ERROR;
}

CHIP_ERROR DiagnosticDataProviderImpl::GetWiFiBeaconLostCount(uint32_t & beaconLostCount)
{
    beaconLostCount = 0;
    return CHIP_NO_ERROR;
}

CHIP_ERROR DiagnosticDataProviderImpl::GetWiFiCurrentMaxRate(uint64_t & currentMaxRate)
{
    currentMaxRate = 0;
    return CHIP_NO_ERROR;
}

CHIP_ERROR DiagnosticDataProviderImpl::GetWiFiPacketMulticastRxCount(uint32_t & packetMulticastRxCount)
{
    packetMulticastRxCount = 0;
    return CHIP_NO_ERROR;
}

CHIP_ERROR DiagnosticDataProviderImpl::GetWiFiPacketMulticastTxCount(uint32_t & packetMulticastTxCount)
{
    packetMulticastTxCount = 0;
    return CHIP_NO_ERROR;
}

CHIP_ERROR DiagnosticDataProviderImpl::GetWiFiPacketUnicastRxCount(uint32_t & packetUnicastRxCount)
{
    packetUnicastRxCount = 0;
    return CHIP_NO_ERROR;
}

CHIP_ERROR DiagnosticDataProviderImpl::GetWiFiPacketUnicastTxCount(uint32_t & packetUnicastTxCount)
{
    packetUnicastTxCount = 0;
    return CHIP_NO_ERROR;
}

CHIP_ERROR DiagnosticDataProviderImpl::GetWiFiOverrunCount(uint64_t & overrunCount)
{
    overrunCount = 0;
    return CHIP_NO_ERROR;
}

CHIP_ERROR DiagnosticDataProviderImpl::ResetWiFiNetworkDiagnosticsCounts()
{
    return CHIP_NO_ERROR;
}
#endif // CHIP_DEVICE_CONFIG_ENABLE_WIFI

} // namespace DeviceLayer
} // namespace chip
