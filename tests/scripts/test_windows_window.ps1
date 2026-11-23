$ErrorActionPreference = 'Stop'
$source = Join-Path $PSScriptRoot '../../collector/window_windows.ps1'
$tokens = $null
$errors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile($source, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw ($errors -join "`n") }
foreach ($definition in $ast.FindAll({ param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ne 'Invoke-Collector' }, $false)) {
    . ([scriptblock]::Create($definition.Extent.Text))
}

function Invoke-Collector([string[]]$Arguments) {
    if (($Arguments -join ',') -ne 'service,config') { throw 'Window did not query the registered configuration.' }
    if ($script:queryError) { throw 'Service query failed.' }
    $script:queries++
    $script:registeredConfig
}

function Test-Path($LiteralPath, $PathType) {
    if ($LiteralPath -cne $script:registeredConfig -or $PathType -ne 'Leaf') { throw 'Window checked the wrong configuration.' }
    $true
}

function Start-Process {
    param($FilePath, $Verb, $WindowStyle, [switch]$Wait, $ArgumentList)
    if ($FilePath -eq 'notepad.exe') {
        if ($ArgumentList -cne ('"' + $script:registeredConfig + '"')) { throw 'Editor opened the wrong configuration.' }
        $script:edited++
        return
    }
    if ($Verb -ne 'RunAs' -or !$Wait) { throw 'Service operation must wait for elevation.' }
    $encoded = ($ArgumentList -split ' ')[-1]
    $script = [Text.Encoding]::Unicode.GetString([Convert]::FromBase64String($encoded))
    $errors = $null
    $tree = [System.Management.Automation.Language.Parser]::ParseInput($script, [ref]$null, [ref]$errors)
    if ($errors.Count) { throw ($errors -join "`n") }
    $commands = @($tree.FindAll({ param($node) $node -is [System.Management.Automation.Language.CommandAst] -and $node.InvocationOperator -eq 'Ampersand' }, $true))
    if ($commands.Count -ne 1) { throw 'Expected one Collector invocation.' }
    $values = @($commands[0].CommandElements | ForEach-Object { $_.Value })
    if ($values[0] -cne $binary -or $values[1] -ne 'service' -or $values[2] -ne $expectedAction) { throw 'Executable path or service action changed during elevation.' }
    if ($expectedAction -eq 'install') {
        if ($values.Count -ne 5 -or $values[3] -ne '--config' -or $values[4] -cne $script:registeredConfig) { throw 'Configuration path changed during elevation.' }
    } elseif ($values.Count -ne 3) { throw 'Uninstall received extra arguments.' }
    $script:verified++
}

$script:verified = 0
$script:queries = 0
$script:edited = 0
$script:queryError = $false
foreach ($directory in @('C:\Users\Alice', 'C:\Users\名字 with spaces', 'C:\Users\O''Brien & `$value; $(exit)')) {
    $binary = "$directory\curio-collector.exe"
    $script:registeredConfig = "$directory\custom\collector.yaml"
    Open-Configuration
    foreach ($expectedAction in @('install', 'uninstall')) { Invoke-Service $expectedAction }
}
$script:queryError = $true
foreach ($operation in @({ Open-Configuration }, { Invoke-Service install })) {
    try { & $operation; throw 'Failed service query was ignored.' }
    catch { if ($_.Exception.Message -ne 'Service query failed.') { throw } }
}
if ($verified -ne 6 -or $queries -ne 6 -or $edited -ne 3) { throw 'Service configuration checks did not complete.' }
'PASS: editor and elevation use the registered configuration; failed queries stop the operation.'
