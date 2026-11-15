$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$supportDirectory = Join-Path $env:APPDATA 'WowType'
$configPath = Join-Path $supportDirectory 'wowtype.conf'
$keyPath = Join-Path $supportDirectory 'translate.key'
$utf8 = New-Object System.Text.UTF8Encoding($false)

function Get-ConfigText {
    if (Test-Path $configPath) { return [IO.File]::ReadAllText($configPath) }
    return "# WowType settings`r`n"
}

function Set-ConfigValue([string]$text, [string]$key, [string]$value) {
    $line = "$key=$value"
    $pattern = "(?m)^\s*$([regex]::Escape($key))\s*=.*$"
    if ([regex]::IsMatch($text, $pattern)) {
        return [regex]::Replace($text, $pattern, $line)
    }
    return $text.TrimEnd() + "`r`n$line`r`n"
}

$current = Get-ConfigText
function Get-ConfigValue([string]$key, [string]$fallback) {
    $match = [regex]::Match($current, "(?m)^\s*$([regex]::Escape($key))\s*=\s*(.*?)\s*$")
    if ($match.Success) { return $match.Groups[1].Value.Trim('"') }
    return $fallback
}

$form = New-Object System.Windows.Forms.Form
$form.Text = 'WowType Settings'
$form.StartPosition = 'CenterScreen'
$form.FormBorderStyle = 'FixedDialog'
$form.MaximizeBox = $false
$form.MinimizeBox = $false
$form.ClientSize = New-Object Drawing.Size(620, 330)
$form.Font = New-Object Drawing.Font('Segoe UI', 9)

$enabled = New-Object System.Windows.Forms.CheckBox
$enabled.Text = 'Enable AI translation'
$enabled.Checked = (Get-ConfigValue 'Translate' 'False') -eq 'True'
$enabled.Location = New-Object Drawing.Point(22, 20)
$enabled.AutoSize = $true
$form.Controls.Add($enabled)

$preload = New-Object System.Windows.Forms.CheckBox
$preload.Text = 'Load language model'
$preload.Checked = (Get-ConfigValue 'PreloadLanguageModel' 'False') -eq 'True'
$preload.Location = New-Object Drawing.Point(260, 20)
$preload.AutoSize = $true
$form.Controls.Add($preload)

$fields = @(
    @{ Key = 'TranslateBaseUrl'; Label = 'API base URL'; Default = 'https://api.openai.com/v1'; Y = 67 },
    @{ Key = 'TranslateModel'; Label = 'Model'; Default = 'gpt-4o-mini'; Y = 113 },
    @{ Key = 'TranslateTarget'; Label = 'Target language'; Default = 'en'; Y = 159 }
)
$inputs = @{}
foreach ($field in $fields) {
    $label = New-Object System.Windows.Forms.Label
    $label.Text = $field.Label
    $label.Location = New-Object Drawing.Point(22, $field.Y + 4)
    $label.Size = New-Object Drawing.Size(130, 24)
    $form.Controls.Add($label)

    $box = New-Object System.Windows.Forms.TextBox
    $box.Location = New-Object Drawing.Point(160, $field.Y)
    $box.Size = New-Object Drawing.Size(430, 26)
    $box.Text = Get-ConfigValue $field.Key $field.Default
    $form.Controls.Add($box)
    $inputs[$field.Key] = $box
}

$keyLabel = New-Object System.Windows.Forms.Label
$keyLabel.Text = 'API key'
$keyLabel.Location = New-Object Drawing.Point(22, 208)
$keyLabel.Size = New-Object Drawing.Size(130, 24)
$form.Controls.Add($keyLabel)
$keyBox = New-Object System.Windows.Forms.TextBox
$keyBox.Location = New-Object Drawing.Point(160, 204)
$keyBox.Size = New-Object Drawing.Size(430, 26)
$keyBox.UseSystemPasswordChar = $true
$form.Controls.Add($keyBox)
$keyHint = New-Object System.Windows.Forms.Label
$keyHint.Text = if (Test-Path $keyPath) { 'Key already saved; leave blank to keep it' } else { 'Optional for local services' }
$keyHint.Location = New-Object Drawing.Point(160, 230)
$keyHint.Size = New-Object Drawing.Size(430, 20)
$form.Controls.Add($keyHint)

$hint = New-Object System.Windows.Forms.Label
$hint.Text = 'Uses an OpenAI-compatible endpoint. The API key is stored separately from settings.'
$hint.Location = New-Object Drawing.Point(22, 260)
$hint.Size = New-Object Drawing.Size(575, 24)
$form.Controls.Add($hint)

$save = New-Object System.Windows.Forms.Button
$save.Text = 'Save'
$save.Location = New-Object Drawing.Point(490, 285)
$save.Size = New-Object Drawing.Size(100, 30)
$save.Add_Click({
    New-Item -ItemType Directory -Force -Path $supportDirectory | Out-Null
    $text = Get-ConfigText
    $text = Set-ConfigValue $text 'Translate' $(if ($enabled.Checked) { 'True' } else { 'False' })
    $text = Set-ConfigValue $text 'PreloadLanguageModel' $(if ($preload.Checked) { 'True' } else { 'False' })
    $text = Set-ConfigValue $text 'TranslateLocal' 'False'
    foreach ($field in $fields) {
        $text = Set-ConfigValue $text $field.Key $inputs[$field.Key].Text.Trim()
    }
    [IO.File]::WriteAllText($configPath, $text, $utf8)

    if (-not [string]::IsNullOrWhiteSpace($keyBox.Text)) {
        [IO.File]::WriteAllText($keyPath, $keyBox.Text.Trim() + "`n", $utf8)
        $sid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
        & icacls.exe $keyPath /inheritance:r /grant:r "*${sid}:(R,W)" | Out-Null
        if ($LASTEXITCODE -ne 0) { throw 'Could not restrict access to the API key file.' }
    }
    [System.Windows.Forms.MessageBox]::Show('Settings saved. Restart WowType in the input switcher to reload them.',
        'WowType', 'OK', 'Information') | Out-Null
    $form.Close()
})
$form.Controls.Add($save)

$cancel = New-Object System.Windows.Forms.Button
$cancel.Text = 'Cancel'
$cancel.Location = New-Object Drawing.Point(380, 285)
$cancel.Size = New-Object Drawing.Size(100, 30)
$cancel.DialogResult = [Windows.Forms.DialogResult]::Cancel
$form.Controls.Add($cancel)
$form.AcceptButton = $save
$form.CancelButton = $cancel
[void]$form.ShowDialog()
