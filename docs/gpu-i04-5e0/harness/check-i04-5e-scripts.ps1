param([string]$Source,[string]$Output)
$ErrorActionPreference='Stop'
$taskResults=@()
foreach($taskName in @('profile-stabilization.ps1','profile-transport.ps1','profile-work.ps1','profile-hot.ps1','profile-cost.ps1')){
    $taskTokens=$null;$taskErrors=$null
    $null=[Management.Automation.Language.Parser]::ParseFile((Join-Path $Source $taskName),[ref]$taskTokens,[ref]$taskErrors)
    if($taskErrors.Count){throw ($taskErrors | Out-String)}
    $taskResults+=[pscustomobject]@{script=$taskName;status='passed'}
}
[ordered]@{powershell_version=$PSVersionTable.PSVersion.ToString();checks=$taskResults} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $Output -Encoding utf8
