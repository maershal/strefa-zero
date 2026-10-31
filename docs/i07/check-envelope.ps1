param([string]$Output,[string]$ValidationOutput)
$ErrorActionPreference='Stop'
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
            $envelope | ConvertTo-Json -Depth 6 | Set-Content -Encoding UTF8 -LiteralPath $ValidationOutput
