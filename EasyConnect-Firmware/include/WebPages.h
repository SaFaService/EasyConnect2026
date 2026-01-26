#ifndef WEBPAGES_H
#define WEBPAGES_H

const char PAGE_DASHBOARD[] PROGMEM = R"=====(
<!DOCTYPE html>
<html>
<head>
    <meta name='viewport' content='width=device-width, initial-scale=1'>
    <style>
        body { font-family: sans-serif; background: #f4f4f4; text-align: center; margin:0; padding:10px; }
        .header { background: #333; color: white; padding: 10px; margin-bottom: 20px; }
        .container { display: flex; flex-wrap: wrap; justify-content: center; }
        .card { background: white; border-radius: 10px; padding: 15px; margin: 10px; width: 280px; box-shadow: 0 4px 6px rgba(0,0,0,0.1); text-align: left; }
        .card h3 { margin-top: 0; color: #007bff; border-bottom: 1px solid #eee; padding-bottom: 5px; }
        .diff-card { width: 90%; max-width: 600px; background: #e7f3ff; border: 2px solid #007bff; text-align: center; }
        .status-dot { height: 15px; width: 15px; border-radius: 50%; display: inline-block; margin-right: 5px; }
        .red { background: #ff4d4d; } .yellow { background: #ffcc00; } .green { background: #28a745; }
        .btn { background: #333; color: white; padding: 12px 20px; border: none; border-radius: 5px; cursor: pointer; margin: 10px; }
        .manutenzione { color: red; font-weight: bold; animation: blink 1s infinite; }
        @keyframes blink { 50% { opacity: 0; } }
    </style>
</head>
<body>
    <div class='header'>
        <span id='net-dot' class='status-dot red'></span>
        EASY CONNECT REWAMPING
    </div>

    <div id='scansione-status' style='display:none; color: orange;'>Scansione RS485 in corso...</div>

    <div class='container' id='diff-container'></div>
    <div class='container' id='slave-container'></div>

    <hr>
    <button class='btn' onclick='location.href="/calibra"'>CALIBRAZIONE</button>
    <button class='btn' onclick='location.href="/wifi"'>IMPOSTAZIONI WIFI</button>

    <script>
        function updateData() {
            fetch('/data').then(response => response.json()).then(data => {
                // Aggiorna pallino internet
                const dot = document.getElementById('net-dot');
                dot.className = 'status-dot ' + (data.internet == 2 ? 'green' : (data.internet == 1 ? 'yellow' : 'red'));
                
                // Aggiorna Scansione
                document.getElementById('scansione-status').style.display = data.scansione ? 'block' : 'none';

                // Quadrati Slave
                let slaveHtml = '';
                data.slaves.forEach(s => {
                    slaveHtml += `<div class='card'>
                        <h3>Slave ${s.addr}</h3>
                        <p><small>SN: ${s.sn}</small></p>
                        <p>Temp: ${s.t} °C</p>
                        <p>Umid: ${s.h} %</p>
                        <p>Press: ${s.p} Pa</p>
                        <p>Sicurezza: <span style='color:${s.sic == 1 ? "green":"red"}'>${s.sic == 1 ? "OK":"ALLARME"}</span></p>
                    </div>`;
                });
                document.getElementById('slave-container').innerHTML = slaveHtml;

                // Quadrato Differenziale
                if(data.diff) {
                    let diffHtml = `<div class='card diff-card'>
                        <h3>PRESSIONE DIFFERENZIALE</h3>
                        <h1 style='margin:10px 0;'>${data.diff.val} Pa</h1>
                        ${data.diff.alert ? "<p class='manutenzione'>EFFETTUARE MANUTENZIONE</p>" : ""}
                    </div>`;
                    document.getElementById('diff-container').innerHTML = diffHtml;
                } else {
                    document.getElementById('diff-container').innerHTML = '';
                }
            });
        }
        setInterval(updateData, 2000);
        updateData();
    </script>
</body>
</html>
)=====";

#endif