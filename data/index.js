var gateway = `ws://${window.location.hostname}/ws`;
var websocket;
var maxRPM = 0;
var prevRPM = 0;
var csvfields = ""
var downloadCounter = 0;
var staticAdvTimeout = null; 


// ----------------------------------------------------------------------------
// Initialization
// ----------------------------------------------------------------------------

document.getElementById("chartContainer").style.display = "none"
document.getElementById("main-button").style.display = "none"
document.getElementById("rst-button").style.display = "none"
document.getElementById("download-button").style.display = "none"

window.addEventListener('load', onLoad);

function onLoad(event) {
    initWebSocket();
    initButton();
}

// Function to download data to a file
function download(data, filename, type) {
    var file = new Blob([data], {type: type});
    if (window.navigator.msSaveOrOpenBlob) // IE10+
        window.navigator.msSaveOrOpenBlob(file, filename);
    else { // Others
        var a = document.createElement("a")
        var url = URL.createObjectURL(file);
        a.href = url;
        a.download = filename;
        document.body.appendChild(a);
        a.click();
        setTimeout(function() {
            document.body.removeChild(a);
            window.URL.revokeObjectURL(url);  
        }, 0);
    }
}

// ----------------------------------------------------------------------------
// WebSocket handling
// ----------------------------------------------------------------------------

function initWebSocket() {
    console.log('Trying to open a WebSocket connection...');
    try {
        websocket = new WebSocket(gateway);
        websocket.onopen    = onOpen;
        websocket.onclose   = onClose;
        websocket.onerror   = onError;
        websocket.onmessage = onMessage;
    } catch (error) {
        console.log('WebSocket not available (testing locally?)', error);
    }
}

function onOpen(event) {
    console.log('Connection opened');
    const statusLed = document.getElementById('status-led');
    if (statusLed) statusLed.className = 'led-green';
    // Request current STATIC_ADV value
    websocket.send(JSON.stringify({'action':'get_static_adv'}));
}

function onClose(event) {
    console.log('Connection closed');
    const statusLed = document.getElementById('status-led');
    if (statusLed) statusLed.className = 'led-red';
    setTimeout(initWebSocket, 2000);
}

function onError(event) {
    console.log('WebSocket error');
}

function onMessage(event) {
    let data = JSON.parse(event.data);
    if (data.status)
        document.getElementById('led').className = data.status;
    if (data.static_adv !== undefined) {
        document.getElementById('static-adv-slider').value = data.static_adv;
        document.getElementById('static-adv-display').textContent = data.static_adv + "°";
    }
    if (data.rpm) {
        if (data.rpm > maxRPM) maxRPM = data.rpm
        document.getElementById('counter').textContent = "RPM: " + data.rpm;
        document.getElementById('maxrpm').textContent = "MAX: " + maxRPM;

        // logging
        if (prevRPM < data.rpm) {
            csvfields = csvfields.concat(data.rpm + ",")
            if (data.adv > 0 && data.adv < 50) {
                csvfields = csvfields.concat(data.adv + ",\n")
            } else {
                csvfields = csvfields.concat("-1,\n")
            }

            document.getElementById('advance').textContent = "ADV: " + data.adv + "°";
            updateChart()
            prevRPM = data.rpm
        }
    }
        
}

// ----------------------------------------------------------------------------
// Button handling
// ----------------------------------------------------------------------------

function initButton() {
    //document.getElementById('toggle').addEventListener('click', onToggle);
    document.getElementById('graph-button').addEventListener('click', graphPage);
    document.getElementById('main-button').addEventListener('click', mainPage);
    document.getElementById('rst-button').addEventListener('click', () => {
        csvfields = ""
        updateChart()
    });
    document.getElementById('download-button').addEventListener('click', () => {
        downloadCounter++
        download(csvfields,"log" + downloadCounter + ".csv", "text/csv")
    });
    
    // Slider with 500ms timeout
    const slider = document.getElementById('static-adv-slider');
    const display = document.getElementById('static-adv-display');
    
    if (slider && display) {
        slider.addEventListener('input', () => {
            const value = parseInt(slider.value);
            display.textContent = value + "°";
            
            // Clear existing timeout
            if (staticAdvTimeout) {
                clearTimeout(staticAdvTimeout);
            }
            
            // Set new timeout to send data after 500ms
            staticAdvTimeout = setTimeout(() => {
                if (websocket && websocket.readyState === WebSocket.OPEN) {
                    websocket.send(JSON.stringify({'action':'set_static_adv', 'value': value}));
                }
            }, 500);
        });
    }
}

function onToggle(event) {
    //websocket.send(JSON.stringify({'action':'toggle'}));
    download(csvfields,"log" + downloadCounter + ".csv", "text/csv")
    console.log(csvfields)
    downloadCounter++;
}

function graphPage() {
    document.getElementById("main-page").style.display = "none"
    document.getElementById("chartContainer").removeAttribute("style")
    document.getElementById("main-button").removeAttribute("style")
    document.getElementById("rst-button").removeAttribute("style")
    document.getElementById("download-button").removeAttribute("style")
    document.body.style.height = "80%"
    resizeCanvas(); // Initial resize
}

function mainPage() {
    document.getElementById("main-page").removeAttribute("style")
    document.getElementById("chartContainer").style.display = "none"
    document.getElementById("main-button").style.display = "none"
    document.getElementById("rst-button").style.display = "none"
    document.getElementById("download-button").style.display = "none"
    document.body.style.height = "100%"
}




// ----------------------------------------------------------------------------
// Graph
// ----------------------------------------------------------------------------

const container = document.getElementById('chartContainer');
const canvas = document.getElementById('chartCanvas');
const ctx = canvas.getContext('2d');
const tooltip = document.getElementById('tooltip');

let dataPoints = [];
// Ensure the canvas matches the container size
function resizeCanvas() {
    canvas.width = container.clientWidth;
    canvas.height = container.clientHeight;
    updateChart()
}
window.addEventListener('resize', resizeCanvas);
window.addEventListener('orientationchange', resizeCanvas);


function parseCSV(data) {
    // Split the string by newline and then by commas
    const rows = data.trim().split('\n');
    return rows.map(row => {
        const [rpm, adv] = row.split(',').map(Number);
        return { rpm, adv };
    });
}
function drawChart() {
    // Clear the canvas
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    // Set up scales
    const maxX = Math.ceil(Math.max(...dataPoints.map(d => d.rpm)) / 100) * 100 || 6000; // Round up to the nearest 100
    const maxY = Math.max(...dataPoints.map(d => d.adv)) || 50;   // Default maxY if no data
    const minX = 0;
    const minY = 0;
    // Margins for labels and padding
    const margin = 50;
    const chartWidth = canvas.width - 2 * margin;
    const chartHeight = canvas.height - 2 * margin;
    // Draw grid lines and labels
    const gridLineCount = parseFloat(getComputedStyle(canvas).getPropertyValue('--gridline-count')) || 5;;
    ctx.strokeStyle = getComputedStyle(canvas).getPropertyValue('--gridline-color') || 'grey';
    ctx.lineWidth = parseFloat(getComputedStyle(canvas).getPropertyValue('--gridline-width')) || 1;
    ctx.setLineDash(getComputedStyle(canvas).getPropertyValue('--gridline-dash').split(',').map(Number) || []);
    ctx.fillStyle = getComputedStyle(canvas).getPropertyValue('--gridtext-color') || 'white';
    ctx.font = getComputedStyle(canvas).getPropertyValue('--gridtext-font') || '14px Arial';
    // Y-axis grid lines and labels (ADV)
    for (let i = 0; i <= gridLineCount; i++) {
        const y = margin + (i * chartHeight) / gridLineCount;
        const advLabel = maxY - (i * maxY) / gridLineCount;
        ctx.beginPath();
        ctx.moveTo(margin, y);
        ctx.lineTo(margin + chartWidth, y);
        ctx.stroke();
        ctx.fillText(advLabel.toFixed(0), margin - 30, y + 5);
    }
    // X-axis grid lines and labels (RPM)
    for (let i = 0; i <= gridLineCount; i++) {
        const x = margin + (i * chartWidth) / gridLineCount;
        const rpmLabel = Math.round((i * maxX) / gridLineCount / 100) * 100; // Round to nearest 100
        ctx.beginPath();
        ctx.moveTo(x, margin);
        ctx.lineTo(x, margin + chartHeight);
        ctx.stroke();
        ctx.fillText(rpmLabel.toFixed(0), x - 15, margin + chartHeight + 25);
    }
    // Axis Labels
    ctx.fillText("ADV", margin - 40, margin - 10);
    ctx.fillText("RPM", margin + chartWidth + 10, margin + chartHeight + 35);
    // Draw the data line with CSS properties
    ctx.strokeStyle = getComputedStyle(canvas).getPropertyValue('--line-color') || 'white';
    ctx.lineWidth = parseFloat(getComputedStyle(canvas).getPropertyValue('--line-width')) || 2;
    ctx.setLineDash(getComputedStyle(canvas).getPropertyValue('--line-dash').split(',').map(Number) || []);
    ctx.beginPath();
    dataPoints.forEach((point, index) => {
        const x = margin + ((point.rpm - minX) / (maxX - minX)) * chartWidth; // Scale to canvas
        const y = margin + chartHeight - ((point.adv - minY) / (maxY - minY)) * chartHeight; // Scale to canvas
        if (index === 0) {
            ctx.moveTo(x, y);
        } else {
            ctx.lineTo(x, y);
        }
        point.canvasX = x;
        point.canvasY = y;
    });
    ctx.stroke();
}
function getNearestDataPoint(mouseX, mouseY) {
    const margin = 50;
    const chartWidth = canvas.width - 2 * margin;
    const chartHeight = canvas.height - 2 * margin;
    return dataPoints.reduce((nearest, point) => {
        const dist = Math.hypot(point.canvasX - mouseX, point.canvasY - mouseY);
        return dist < nearest.dist ? { point, dist } : nearest;
    }, { point: null, dist: Infinity }).point;
}
function drawCrosshair(point) {
    if (!point) return;
    const margin = 50;
    const chartWidth = canvas.width - 2 * margin;
    const chartHeight = canvas.height - 2 * margin;
    // Draw the tooltip lines with CSS properties
    ctx.strokeStyle = getComputedStyle(canvas).getPropertyValue('--tipline-color') || 'grey';
    ctx.lineWidth = parseFloat(getComputedStyle(canvas).getPropertyValue('--tipline-width')) || 2;
    ctx.setLineDash(getComputedStyle(canvas).getPropertyValue('--tipline-dash').split(',').map(Number) || []);
    ctx.setLineDash([5, 5]);
    // Horizontal line (ADV)
    ctx.beginPath();
    ctx.moveTo(margin, point.canvasY);
    ctx.lineTo(margin + chartWidth, point.canvasY);
    ctx.stroke();
    // Vertical line (RPM)
    ctx.beginPath();
    ctx.moveTo(point.canvasX, margin);
    ctx.lineTo(point.canvasX, margin + chartHeight);
    ctx.stroke();
    ctx.setLineDash([]);
    // Show tooltip
    tooltip.style.display = 'block';
    tooltip.style.left = `${point.canvasX + 15}px`;
    tooltip.style.top = `${point.canvasY - 25}px`;
    tooltip.textContent = `RPM: ${point.rpm}, ADV: ${point.adv}`;
}
function hideTooltip() {
    tooltip.style.display = 'none';
}
function handleMouseMove(event) {
    const rect = canvas.getBoundingClientRect();
    const mouseX = event.clientX - rect.left;
    const mouseY = event.clientY - rect.top;
    const nearestPoint = getNearestDataPoint(mouseX, mouseY);
    drawChart();
    drawCrosshair(nearestPoint);
}
function handleTouchMove(event) {
    const rect = canvas.getBoundingClientRect();
    const touch = event.touches[0];
    const touchX = touch.clientX - rect.left;
    const touchY = touch.clientY - rect.top;
    const nearestPoint = getNearestDataPoint(touchX, touchY);
    drawChart();
    drawCrosshair(nearestPoint);
}
function updateChart() {
    dataPoints = parseCSV(csvfields);
    drawChart();
}
canvas.addEventListener('mousemove', handleMouseMove);
canvas.addEventListener('touchmove', handleTouchMove);
canvas.addEventListener('mouseout', hideTooltip);
canvas.addEventListener('touchend', hideTooltip);
