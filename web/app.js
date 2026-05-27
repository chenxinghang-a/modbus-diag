// Modbus Diagnostic Tool - Web Interface

async function loadReport() {
    try {
        const resp = await fetch('/report');
        if (!resp.ok) throw new Error('No report');
        const data = await resp.json();
        displayReport(data);
    } catch (e) {
        document.getElementById('report').innerHTML =
            '<p style="color:#ff4444">No diagnostic report available. Run a diagnostic on the device first.</p>';
    }
}

function displayReport(data) {
    const gradeClass = data.health_score >= 90 ? 'excellent' :
                       data.health_score >= 75 ? 'good' :
                       data.health_score >= 60 ? 'fair' : 'poor';

    let html = `
        <div class="score ${gradeClass}">${data.health_score}/100</div>
        <div class="metric"><span class="label">Grade</span><span class="value">${data.grade}</span></div>
        <div class="metric"><span class="label">Device</span><span class="value">${data.device}</span></div>
        <hr style="border-color:#2a2a5e;margin:10px 0">
        <h4 style="color:#888;margin-bottom:8px">Response Time</h4>
        <div class="metric"><span class="label">Min</span><span class="value">${data.response.min_us} us</span></div>
        <div class="metric"><span class="label">Avg</span><span class="value">${data.response.avg_us} us</span></div>
        <div class="metric"><span class="label">Max</span><span class="value">${data.response.max_us} us</span></div>
        <div class="metric"><span class="label">Jitter</span><span class="value">${data.response.jitter_us} us</span></div>
        <hr style="border-color:#2a2a5e;margin:10px 0">
        <h4 style="color:#888;margin-bottom:8px">Traffic</h4>
        <div class="metric"><span class="label">Total Frames</span><span class="value">${data.traffic.total_frames}</span></div>
        <div class="metric"><span class="label">Errors</span><span class="value">${data.traffic.error_frames}</span></div>
        <div class="metric"><span class="label">Exceptions</span><span class="value">${data.traffic.exception_frames}</span></div>
        <div class="metric"><span class="label">Error Rate</span><span class="value">${(data.traffic.error_rate * 100).toFixed(2)}%</span></div>
    `;
    document.getElementById('report').innerHTML = html;
}

async function downloadReport() {
    try {
        const resp = await fetch('/report');
        const blob = await resp.blob();
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = 'modbus_report.json';
        a.click();
    } catch (e) {
        alert('No report available');
    }
}

function downloadHTML() {
    // Generate HTML report client-side
    fetch('/report')
        .then(r => r.json())
        .then(data => {
            const html = generateHTMLReport(data);
            const blob = new Blob([html], { type: 'text/html' });
            const url = URL.createObjectURL(blob);
            const a = document.createElement('a');
            a.href = url;
            a.download = 'modbus_report.html';
            a.click();
        })
        .catch(() => alert('No report available'));
}

function generateHTMLReport(data) {
    return `<!DOCTYPE html><html><head><meta charset="utf-8"><title>Report</title></head>
<body style="font-family:monospace;background:#1a1a2e;color:#eee;padding:20px">
<h1>Modbus Diagnostic Report</h1>
<h2>Health Score: ${data.health_score}/100 (${data.grade})</h2>
<p>Device: ${data.device}</p>
<h3>Response Time</h3>
<p>Min: ${data.response.min_us}us | Avg: ${data.response.avg_us}us | Max: ${data.response.max_us}us</p>
<h3>Traffic</h3>
<p>Frames: ${data.traffic.total_frames} | Errors: ${data.traffic.error_frames} | Rate: ${(data.traffic.error_rate*100).toFixed(2)}%</p>
</body></html>`;
}

// Auto-load on page load
loadReport();
