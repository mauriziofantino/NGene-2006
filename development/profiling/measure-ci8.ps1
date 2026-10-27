param(
    [Parameter(Mandatory=$true)][string]$InputFile,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [ValidateRange(1,20000000)][int]$SampleRate = 8183800
)
$ErrorActionPreference='Stop'
if (-not ('Ci8Diagnostics' -as [type])) {
Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Collections.Generic;
public sealed class Ci8Window {
    public int Index;
    public long Bytes;
    public double MeanI, MeanQ, RmsI, RmsQ, CentredRms;
    public double RailPercentI, RailPercentQ, NearRailPercentI, NearRailPercentQ;
}
public sealed class Ci8Result {
    public long Bytes;
    public long[][] Histogram;
    public List<Ci8Window> Windows;
    public Ci8Window Total;
}
public static class Ci8Diagnostics {
    static Ci8Window Summarize(int index, long bytes, long[] sum, long[] square, long[] rail, long[] near) {
        double n=bytes/2.0;
        double mi=sum[0]/n, mq=sum[1]/n;
        double vi=square[0]/n-mi*mi, vq=square[1]/n-mq*mq;
        return new Ci8Window { Index=index, Bytes=bytes, MeanI=mi, MeanQ=mq,
            RmsI=Math.Sqrt(square[0]/n), RmsQ=Math.Sqrt(square[1]/n),
            CentredRms=Math.Sqrt((vi+vq)/2), RailPercentI=100*rail[0]/n,
            RailPercentQ=100*rail[1]/n, NearRailPercentI=100*near[0]/n,
            NearRailPercentQ=100*near[1]/n };
    }
    public static Ci8Result Measure(string filename, int sampleRate) {
        long[][] hist={new long[256],new long[256]};
        long[] sum=new long[2], square=new long[2], rail=new long[2], near=new long[2];
        long[] ws=new long[2], wq=new long[2], wr=new long[2], wn=new long[2];
        var windows=new List<Ci8Window>();
        byte[] buffer=new byte[4*1024*1024];
        long total=0, windowBytes=0, width=2L*sampleRate;
        int component=0, count;
        using(var input=File.OpenRead(filename)) {
            if(input.Length==0 || (input.Length&1)!=0) throw new ArgumentException("CI8 input must contain complete I/Q pairs");
            while((count=input.Read(buffer,0,buffer.Length))>0) {
                for(int i=0;i<count;++i) {
                    int value=(sbyte)buffer[i];
                    hist[component][value+128]++;
                    sum[component]+=value; square[component]+=value*value;
                    ws[component]+=value; wq[component]+=value*value;
                    if(value==-128 || value==127) {rail[component]++; wr[component]++;}
                    if(value<=-120 || value>=120) {near[component]++; wn[component]++;}
                    component^=1;
                    total++; windowBytes++;
                    if(windowBytes==width) {
                        windows.Add(Summarize(windows.Count,windowBytes,ws,wq,wr,wn));
                        Array.Clear(ws,0,2); Array.Clear(wq,0,2); Array.Clear(wr,0,2); Array.Clear(wn,0,2);
                        windowBytes=0;
                    }
                }
            }
        }
        if(windowBytes>0) windows.Add(Summarize(windows.Count,windowBytes,ws,wq,wr,wn));
        return new Ci8Result {Bytes=total,Histogram=hist,Windows=windows,Total=Summarize(-1,total,sum,square,rail,near)};
    }
}
'@
}
$taskInput=(Resolve-Path -LiteralPath $InputFile).Path
$taskOutput=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force $taskOutput | Out-Null
$taskResult=[Ci8Diagnostics]::Measure($taskInput,$SampleRate)
$taskResult.Windows | Export-Csv -LiteralPath (Join-Path $taskOutput 'iq-per-second.csv') -NoTypeInformation
$taskSummary=[ordered]@{Input=$taskInput;SampleRate=$SampleRate;SignalSeconds=$taskResult.Bytes/(2.0*$SampleRate);Statistics=$taskResult.Total;Histogram=$taskResult.Histogram}
[IO.File]::WriteAllText((Join-Path $taskOutput 'iq-summary.json'),(ConvertTo-Json -InputObject $taskSummary -Depth 6),(New-Object Text.UTF8Encoding($false)))
$taskResult.Total
