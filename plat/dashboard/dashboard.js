const PLACEHOLDER_HARDWARE_LATENCY_US = 1.8;
// TODO(RTL-T4): replace this simulated placeholder with measured driver latency.

const fields = {
    angle: document.querySelector('#angle'),
    sine: document.querySelector('#sine'),
    cosine: document.querySelector('#cosine'),
    target: document.querySelector('#target'),
    sampleLatency: document.querySelector('#sample-latency'),
    softwareAverage: document.querySelector('#software-average'),
    hardwareLatency: document.querySelector('#hardware-latency'),
    speedup: document.querySelector('#speedup'),
    state: document.querySelector('#connection-state'),
};

function format(value, digits = 3) {
    return Number(value).toFixed(digits);
}

const telemetry = new EventSource('/events');
telemetry.onopen = () => { fields.state.textContent = 'Live software stream'; };
telemetry.onerror = () => { fields.state.textContent = 'Reconnecting to telemetry stream…'; };
telemetry.onmessage = event => {
    const sample = JSON.parse(event.data);
    fields.angle.textContent = `${format(sample.angle_degrees)}°`;
    fields.sine.textContent = format(sample.sin, 6);
    fields.cosine.textContent = format(sample.cos, 6);
    fields.target.textContent = `${sample.path} · ${sample.target}`;
    fields.sampleLatency.textContent = `${format(sample.latency_us)} µs`;
    fields.softwareAverage.textContent = `${format(sample.average_latency_us)} µs`;
    fields.hardwareLatency.textContent = `${format(PLACEHOLDER_HARDWARE_LATENCY_US)} µs`;
    const speedup = sample.average_latency_us / PLACEHOLDER_HARDWARE_LATENCY_US;
    fields.speedup.textContent = `${format(speedup, 2)}× (simulated placeholder)`;
};
