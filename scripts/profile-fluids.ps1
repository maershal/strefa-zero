param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [Parameter(Mandatory=$true)][string]$Output,
    [ValidateSet('quality','benchmark')][string]$Suite='quality'
)
$ErrorActionPreference='Stop'
$Executable=(Resolve-Path -LiteralPath $Executable).Path
if(Test-Path -LiteralPath $Output){throw 'Use a new output directory; previous trials are immutable.'}
$Output=[IO.Path]::GetFullPath($Output)
[IO.Directory]::CreateDirectory($Output) | Out-Null
[ordered]@{schema=1;revision='I07.1';suite=$Suite;quality_ticks=1200;checkpoints=60;settling_window=@(600,1200);compression_limit=0.05;mission_median_budget_ms=0.25;benchmark_warmup=120;benchmark_samples=240;benchmark_blocks=3;order=@('legacy','balanced','balanced','legacy');automatic_promotion=$false} | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 -LiteralPath (Join-Path $Output 'protocol.json')
function Invoke-FluidTrial([string]$Name,[string[]]$Flags){
    $folder=Join-Path $Output $Name
    [IO.Directory]::CreateDirectory($folder) | Out-Null
    $arguments=@($Flags)+@('--profile',('"'+$folder+'"'))
    $spec=[ordered]@{name=$Name;executable=$Executable;sha256=(Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash.ToLowerInvariant();flags=$Flags;started_utc=[DateTime]::UtcNow.ToString('o')}
    $spec | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 -LiteralPath (Join-Path $folder 'run-spec.json')
    $process=Start-Process -FilePath $Executable -ArgumentList $arguments -PassThru
    $timeout=-not $process.WaitForExit(600000)
    if($timeout){$process.Kill();$process.WaitForExit()}
    $process.Refresh()
    [ordered]@{exit_code=$process.ExitCode;timed_out=$timeout;pid=$process.Id;finished_utc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json | Set-Content -Encoding UTF8 -LiteralPath (Join-Path $folder 'run-result.json')
    Write-Host "$Name : $($process.ExitCode)"
    # A failed candidate remains evidence; continue the predeclared matrix.
    return ($process.ExitCode -eq 0 -and -not $timeout)
}
if($Suite -eq 'quality'){
    $cases=@(
        @{name='legacy-a';args=@()},@{name='legacy-b';args=@()},
        @{name='boundary';args=@('--gpu-fluid-boundary','volume')},
        @{name='scorr-0005';args=@('--gpu-fluid-s-corr','0.0005')},
        @{name='scorr-002';args=@('--gpu-fluid-s-corr','0.002')},
        @{name='scorr-008';args=@('--gpu-fluid-s-corr','0.008')},
        @{name='four-substeps';args=@('--gpu-fluid-substeps','4')},
        @{name='surface-quarter';args=@('--gpu-fluid-surface','akinci2d','--gpu-fluid-tension-scale','0.25','--gpu-fluid-curvature-scale','0')},
        @{name='surface';args=@('--gpu-fluid-surface','akinci2d')},
        @{name='adhesion';args=@('--gpu-fluid-adhesion','1')},
        @{name='vorticity';args=@('--gpu-fluid-vorticity','1')},
        @{name='balanced';args=@('--gpu-fluid-profile','balanced')}
    )
    foreach($case in $cases){
        $ok=Invoke-FluidTrial $case.name (@('--gpu-fluid-check','--gpu-fluid-profile','legacy')+$case.args)
        if(($case.name -eq 'legacy-a' -or $case.name -eq 'legacy-b') -and -not $ok){throw 'Baseline quality failed; candidate comparisons are not admissible.'}
        if($case.name -eq 'legacy-b'){
            $reference=@{}
            foreach($name in @('legacy-a','legacy-b')){
                $reference[$name]=@{}
                $groups=Import-Csv -LiteralPath (Join-Path $Output "$name/fluid-quality.csv") | Where-Object {[int]$_.tick -ge 600} | Group-Object scene
                if($groups.Count -ne 7 -or @($groups | Where-Object {$_.Count -ne 11}).Count -ne 0){throw 'Incomplete baseline checkpoints.'}
                foreach($group in $groups){
                    $heights=$group.Group | ForEach-Object {[double]$_.surface_mean} | Measure-Object -Minimum -Maximum
                    $reference[$name][$group.Name]=@{
                        compression_max=($group.Group | ForEach-Object {[double]$_.compression} | Measure-Object -Maximum).Maximum
                        surface_spatial_rms_mean=($group.Group | ForEach-Object {[double]$_.surface_spatial_rms} | Measure-Object -Average).Average
                        surface_temporal_amplitude=$heights.Maximum-$heights.Minimum
                        cluster_fraction_mean=($group.Group | ForEach-Object {[double]$_.cluster_fraction} | Measure-Object -Average).Average
                        kinetic_energy_mean=($group.Group | ForEach-Object {[double]$_.kinetic_energy} | Measure-Object -Average).Average
                    }
                }
            }
            $envelope=@{}
            foreach($scene in $reference['legacy-a'].Keys){
                $envelope[$scene]=@{}
                foreach($metric in $reference['legacy-a'][$scene].Keys){
                    $values=@($reference['legacy-a'][$scene][$metric],$reference['legacy-b'][$scene][$metric]) | Measure-Object -Minimum -Maximum
                    $envelope[$scene][$metric]=@($values.Minimum,$values.Maximum)
                }
            }
            $envelope | ConvertTo-Json -Depth 6 | Set-Content -Encoding UTF8 -LiteralPath (Join-Path $Output 'baseline-envelope.json')
        }
    }
}else{
    for($block=1;$block -le 3;$block++){
        $ordinal=0
        foreach($preset in @('legacy','balanced','balanced','legacy')){
            $ordinal++
            $null=Invoke-FluidTrial "block-$block-$ordinal-$preset" @('--gpu-fluid-benchmark','--gpu-fluid-profile',$preset)
        }
    }
}
