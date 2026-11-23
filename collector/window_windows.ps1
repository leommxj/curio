$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding
Add-Type -AssemblyName System.Windows.Forms
[System.Windows.Forms.Application]::EnableVisualStyles()
$binary = $env:CURIO_COLLECTOR_EXE

function Invoke-Collector([string[]]$Arguments) {
    if (!(Test-Path -LiteralPath $Binary -PathType Leaf)) { throw "Collector not found: $Binary" }
    $ErrorActionPreference = 'Continue'
    $output = & $Binary @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) { throw ($output -join "`n") }
    $output -join "`n"
}

function Get-Configuration { Invoke-Collector -Arguments @('service', 'config') }

function Open-Configuration {
    $config = Get-Configuration
    if (!(Test-Path -LiteralPath $config -PathType Leaf)) { throw 'Initialize configuration first.' }
    Start-Process notepad.exe -ArgumentList "`"$config`""
}

function Invoke-Service([string]$Action) {
    $command = "& '$($binary.Replace("'", "''"))' service $Action"
    if ($Action -eq 'install') {
        $config = Get-Configuration
        $command += " --config '$($config.Replace("'", "''"))'"
    }
    $script = @"
Add-Type -AssemblyName System.Windows.Forms
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding
try {
    `$result = $command 2>&1
    if (`$LASTEXITCODE -ne 0) { throw (`$result -join "``n") }
    [void][System.Windows.Forms.MessageBox]::Show('Service $Action completed.', 'Curio Collector')
} catch { [void][System.Windows.Forms.MessageBox]::Show(`$_.Exception.Message, 'Curio Collector', 'OK', 'Error') }
"@
    $encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($script))
    Start-Process "$PSHOME\powershell.exe" -Verb RunAs -WindowStyle Hidden -Wait -ArgumentList "-NoProfile -STA -NonInteractive -EncodedCommand $encoded"
}

$form = New-Object System.Windows.Forms.Form -Property @{
    Text = 'Curio Collector'
    Font = New-Object System.Drawing.Font('Segoe UI', 10)
    AutoSize = $true
    AutoSizeMode = 'GrowAndShrink'
    FormBorderStyle = 'FixedDialog'
    MaximizeBox = $false
    StartPosition = 'CenterScreen'
}
$panel = New-Object System.Windows.Forms.FlowLayoutPanel -Property @{
    AutoSize = $true
    FlowDirection = 'TopDown'
    WrapContents = $false
    Padding = New-Object System.Windows.Forms.Padding(12)
}
$form.Controls.Add($panel)
$services = New-Object System.Windows.Forms.ContextMenuStrip
foreach ($action in @('Install', 'Manage', 'Uninstall')) {
    $item = $services.Items.Add($action)
    $item.Add_Click({
        param($sender, $eventArgs)
        try {
            if ($sender.Text -eq 'Manage') { Start-Process "$env:SystemRoot\System32\mmc.exe" -Verb RunAs -ArgumentList "`"$env:SystemRoot\System32\services.msc`"" }
            else { Invoke-Service $sender.Text.ToLowerInvariant() }
        } catch { [void][System.Windows.Forms.MessageBox]::Show($form, $_.Exception.Message, 'Curio Collector', 'OK', 'Error') }
    })
}
$actions = [ordered]@{
    'Initialize' = { $null = Invoke-Collector -Arguments @('init', '--config', (Get-Configuration)); Open-Configuration }
    'Edit configuration' = { Open-Configuration }
    'Service' = { $services.Show([System.Windows.Forms.Cursor]::Position) }
}
foreach ($name in $actions.Keys) {
    $button = New-Object System.Windows.Forms.Button -Property @{
        Text = $name; Width = 240; Height = 36; Tag = $actions[$name]
    }
    $button.Add_Click({
        param($sender, $eventArgs)
        try { & $sender.Tag }
        catch { [void][System.Windows.Forms.MessageBox]::Show($form, $_.Exception.Message, 'Curio Collector', 'OK', 'Error') }
    })
    $panel.Controls.Add($button)
}
[void]$form.ShowDialog()
$form.Dispose()
