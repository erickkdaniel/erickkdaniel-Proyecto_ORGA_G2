const express = require('express');
const morgan = require('morgan');
const fs = require('fs');
const cors = require('cors'); 
const path = require('path'); 
const { SerialPort } = require('serialport');
const { ReadlineParser } = require('@serialport/parser-readline');

const app = express();
app.use(cors());
app.use(express.json());
app.use(morgan('dev'));

const PORT = 3000;
const STATE_FILE = path.join(__dirname, 'system_state.json');
const STATS_FILE = path.join(__dirname, 'parking_stats.json');
const SECURE_PASSWORD = 'EDOC'; 

// Define valid modes
const VALID_MODES = ['normal', 'panic', 'nocturnal', 'maintenance', 'evacuation'];

// Load system state
let systemState = loadSystemState();



    serialPort.on('close', () => {
        console.log('Conexión serial cerrada. Intentando reconectar en 5 segundos...');
        setTimeout(connectToArduino, 5000);
    });

    serialPort.on('error', (err) => {
        console.error('Error en el puerto serial:', err.message);
    });


connectToArduino();

// Comunicación con Arduino
function sendModeToArduino(mode) {
    if (serialPort && serialPort.isOpen) {
        const command = `MODE_${mode.toUpperCase()}\r\n`;
        console.log(`Enviando comando de modo al Arduino: ${command.trim()}`);
        serialPort.write(command, (err) => {
            if (err) {
                return console.error('Error escribiendo al puerto serial:', err.message);
            }
            console.log(`Comando enviado exitosamente: ${command.trim()}`);
        });
    } else {
        console.warn('Puerto serial no está abierto para enviar comando de modo.');
    }
}

// Enviar actualizaciones de parqueo a clientes SSE
function sendParkingUpdateToAllClients() {
    const dataToSend = JSON.stringify(parkingDataSummary);
    clients.forEach(client => {
        client.write(`data: ${dataToSend}\n\n`);
    });
    console.log('Datos de parqueo enviados a', clients.length, 'clientes SSE.');
}

// Endpoint para SSE
app.get('/api/parking-stream', (req, res) => {
    res.setHeader('Content-Type', 'text/event-stream');
    res.setHeader('Cache-Control', 'no-cache');
    res.setHeader('Connection', 'keep-alive');

    res.write(`event: modeStatus\ndata: ${JSON.stringify({ mode: systemState.mode })}\n\n`);

    clients.push(res);
    console.log('Nuevo cliente SSE conectado. Total:', clients.length);

    if (systemState.mode === 'normal') {
        res.write(`data: ${JSON.stringify(parkingDataSummary)}\n\n`);
    } else {
        console.log(`Cliente SSE conectado en modo ${systemState.mode}. No se enviarán datos iniciales de parqueo.`);
    }
    
    req.on('close', () => {
        const index = clients.indexOf(res);
        if (index > -1) {
            clients.splice(index, 1);
            console.log('Cliente SSE desconectado. Total:', clients.length);
        }
    });
});

// Endpoint GET para estado de parqueo
app.get('/api/parking-status', (req, res) => {
    if (systemState.mode !== 'normal') {
        return res.status(403).json({ message: `Sistema en modo ${systemState.mode}. Interacciones de parqueo bloqueadas.` });
    }
    res.json(parkingDataSummary);
});

// Endpoint para estadísticas
app.get('/api/parking-stats', (req, res) => {
    if (systemState.mode !== 'normal') {
        return res.status(403).json({ message: `Sistema en modo ${systemState.mode}. Estadísticas no disponibles.` });
    }
    const averageCarsPerHour = calculateAverageCarsPerHour();
    const chartData = parkingStats.map(stat => ({
        timestamp: stat.timestamp,
        occupied: stat.occupied
    }));
    res.json({
        average_cars_per_hour: parseFloat(averageCarsPerHour.toFixed(2)),
        chart_data: chartData
    });
});

// Endpoint para control de modos
app.post('/api/mode-control', (req, res) => {
    const { action, mode, password } = req.body;

    if (action === 'activate') {
        if (!mode || !VALID_MODES.includes(mode)) {
            return res.status(400).json({ success: false, message: "Modo inválido. Debe ser uno de: " + VALID_MODES.join(', ') });
        }
        if (systemState.mode === mode) {
            return res.status(400).json({ success: false, message: `El modo ${mode} ya está activo.` });
        }
        systemState.mode = mode;
        saveSystemState();
        console.log(`MODO ${mode.toUpperCase()} ACTIVADO!`);
        
        sendModeToArduino(mode);
        clients.forEach(client => {
            client.write(`event: modeStatus\ndata: ${JSON.stringify({ mode: systemState.mode })}\n\n`);
        });
        if (mode !== 'normal') {
            clients.forEach(client => {
                client.write(`data: ${JSON.stringify(null)}\n\n`);
            });
        } else {
            sendParkingUpdateToAllClients();
        }
        return res.status(200).json({ success: true, message: `Modo ${mode} activado.` });
    } else if (action === 'deactivate') {
        if (password !== SECURE_PASSWORD) {
            return res.status(401).json({ success: false, message: "Contraseña incorrecta." });
        }
        if (systemState.mode === 'normal') {
            return res.status(400).json({ success: false, message: "El sistema ya está en modo normal." });
        }
        systemState.mode = 'normal';
        saveSystemState();
        console.log("MODO NORMAL RESTAURADO!");
        sendModeToArduino('normal');
        clients.forEach(client => {
            client.write(`event: modeStatus\ndata: ${JSON.stringify({ mode: systemState.mode })}\n\n`);
        });
        sendParkingUpdateToAllClients();
        return res.status(200).json({ success: true, message: "Modo normal restaurado." });
    } else if (action === 'status') {
        return res.status(200).json({ mode: systemState.mode });
    } else {
        return res.status(400).json({ success: false, message: "Acción inválida." });
    }
});

// Iniciar el servidor
app.listen(PORT, () => {
    console.log(`Servidor Node.js escuchando en http://localhost:${PORT}`);
});