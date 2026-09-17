$ErrorActionPreference = 'Stop'
$null = [Windows.UI.Notifications.ToastNotificationManager, Windows.UI.Notifications, ContentType = WindowsRuntime]
$null = [Windows.Data.Xml.Dom.XmlDocument, Windows.Data.Xml.Dom.XmlDocument, ContentType = WindowsRuntime]
$appId = '{1AC14E77-02E7-4E5D-B744-2EB1AE5198B7}\WindowsPowerShell\v1.0\powershell.exe'
if ($env:WINISLAND_TEST_REMOVE -eq '1') {
    [Windows.UI.Notifications.ToastNotificationManager]::History.Remove('WinIslandTest', 'WinIslandTest', $appId)
    Write-Output 'Removed only the synthetic WinIsland test notification.'
    exit
}
$xml = New-Object Windows.Data.Xml.Dom.XmlDocument
$xml.LoadXml('<toast duration="short"><visual><binding template="ToastGeneric"><text>WinIsland 集成测试</text><text>这是一条用于验证灵动岛的 Windows 系统通知。</text></binding></visual><audio silent="true"/></toast>')
$toast = [Windows.UI.Notifications.ToastNotification]::new($xml)
$toast.Tag = 'WinIslandTest'
$toast.Group = 'WinIslandTest'
$toast.ExpirationTime = [DateTimeOffset]::Now.AddMinutes(2)
$notifier = [Windows.UI.Notifications.ToastNotificationManager]::CreateToastNotifier($appId)
$notifier.Show($toast)
Write-Output 'Synthetic Windows notification submitted.'
