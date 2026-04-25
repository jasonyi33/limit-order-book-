const SAMPLE_TRACE_URL = "./data/sample_orders.trace.json";

const state = {
    trace: null,
    frames: [],
    currentIndex: 0,
    isPlaying: false,
    playbackRate: 1,
    playTimer: null,
};

const elements = {
    loadSample: document.getElementById("load-sample"),
    traceUpload: document.getElementById("trace-upload"),
    statusMessage: document.getElementById("status-message"),
    summarySource: document.getElementById("summary-source"),
    summaryFrames: document.getElementById("summary-frames"),
    metricProcessed: document.getElementById("metric-processed"),
    metricTrades: document.getElementById("metric-trades"),
    metricSpread: document.getElementById("metric-spread"),
    metricRuntime: document.getElementById("metric-runtime"),
    frameCounter: document.getElementById("frame-counter"),
    frameStep: document.getElementById("frame-step"),
    depthRange: document.getElementById("depth-range"),
    timestampReadout: document.getElementById("timestamp-readout"),
    timeline: document.getElementById("timeline"),
    stepBack: document.getElementById("step-back"),
    playToggle: document.getElementById("play-toggle"),
    stepForward: document.getElementById("step-forward"),
    speedSelect: document.getElementById("speed-select"),
    midPrice: document.getElementById("mid-price"),
    spreadChip: document.getElementById("spread-chip"),
    stateHeadline: document.getElementById("state-headline"),
    stateSubline: document.getElementById("state-subline"),
    askLadder: document.getElementById("ask-ladder"),
    bidLadder: document.getElementById("bid-ladder"),
    eventBadges: document.getElementById("event-badges"),
    eventHeadline: document.getElementById("event-headline"),
    eventDescription: document.getElementById("event-description"),
    eventLatency: document.getElementById("event-latency"),
    eventMatched: document.getElementById("event-matched"),
    eventRemaining: document.getElementById("event-remaining"),
    eventActiveOrders: document.getElementById("event-active-orders"),
    tradeCount: document.getElementById("trade-count"),
    tradeList: document.getElementById("trade-list"),
    spreadChartValue: document.getElementById("spread-chart-value"),
    volumeChartValue: document.getElementById("volume-chart-value"),
    spreadChart: document.getElementById("spread-chart"),
    volumeChart: document.getElementById("volume-chart"),
};

function formatNumber(value) {
    return new Intl.NumberFormat("en-US").format(value);
}

function formatPrice(cents) {
    if (cents == null || cents < 0) {
        return "N/A";
    }

    return new Intl.NumberFormat("en-US", {
        style: "currency",
        currency: "USD",
        minimumFractionDigits: 2,
        maximumFractionDigits: 2,
    }).format(cents / 100);
}

function formatLatency(ns) {
    if (ns == null) {
        return "N/A";
    }

    if (ns >= 1_000_000) {
        return `${(ns / 1_000_000).toFixed(2)} ms`;
    }

    if (ns >= 1_000) {
        return `${(ns / 1_000).toFixed(2)} us`;
    }

    return `${ns} ns`;
}

function formatRuntime(ns) {
    if (ns == null) {
        return "0 ms";
    }

    if (ns >= 1_000_000) {
        return `${(ns / 1_000_000).toFixed(3)} ms`;
    }

    return `${(ns / 1_000).toFixed(2)} us`;
}

function safeSpread(frame) {
    return frame?.spread != null && frame.spread >= 0 ? formatPrice(frame.spread) : "N/A";
}

function sideLabel(side) {
    return side === "BUY" ? "Buy" : "Sell";
}

function typeLabel(type) {
    return type === "LIMIT" ? "Limit" : "Market";
}

function actionLabel(action) {
    return action === "ADD" ? "Add" : "Cancel";
}

function describeEvent(frame) {
    const { action, event, matchedQuantity, remainingQuantity, cancelSucceeded } = frame;
    const order = event;
    const side = sideLabel(order.side);
    const type = typeLabel(order.type);
    const priceText = order.type === "MARKET" ? "at market" : `@ ${formatPrice(order.price)}`;

    if (action === "CANCEL") {
        return cancelSucceeded
            ? `Canceled resting order #${order.orderId}.`
            : `Cancel request for order #${order.orderId} missed because that order was no longer active.`;
    }

    if (matchedQuantity === 0 && order.type === "LIMIT") {
        return `${side} ${type} order #${order.orderId} rested on the book ${priceText}.`;
    }

    if (matchedQuantity > 0 && remainingQuantity > 0 && order.type === "LIMIT") {
        return `${side} ${type} order #${order.orderId} matched ${formatNumber(matchedQuantity)} and rested ${formatNumber(remainingQuantity)} ${priceText}.`;
    }

    if (matchedQuantity > 0 && remainingQuantity === 0) {
        return `${side} ${type} order #${order.orderId} fully executed ${formatNumber(matchedQuantity)}.`;
    }

    if (order.type === "MARKET" && remainingQuantity > 0) {
        return `${side} market order #${order.orderId} consumed ${formatNumber(matchedQuantity)} and dropped the unfilled remainder when the opposite book ran out.`;
    }

    return `${side} ${type} order #${order.orderId} updated the book.`;
}

function headlineForFrame(frame) {
    const { action, event } = frame;
    const order = event;

    if (action === "CANCEL") {
        return `Cancel order #${order.orderId}`;
    }

    const side = sideLabel(order.side);
    const type = typeLabel(order.type);
    const quantity = formatNumber(order.quantity);
    const priceText = order.type === "MARKET" ? "at market" : `@ ${formatPrice(order.price)}`;
    return `${side} ${type} ${quantity} ${priceText}`;
}

function stateNarrative(frame) {
    if (!frame) {
        return {
            headline: "Waiting for trace",
            subline: "Load a trace to begin playback.",
        };
    }

    if (frame.trades.length > 0) {
        return {
            headline: `${frame.trades.length} trade${frame.trades.length === 1 ? "" : "s"} just printed`,
            subline: `Cumulative traded volume is now ${formatNumber(frame.cumulativeTradedQuantity)} units.`,
        };
    }

    if (frame.action === "CANCEL") {
        return {
            headline: frame.cancelSucceeded ? "Liquidity pulled from the book" : "Cancel request missed",
            subline: frame.cancelSucceeded
                ? "A resting order was removed without generating any trade."
                : "The cancel referred to an order that was already gone.",
        };
    }

    return {
        headline: frame.remainingQuantity > 0 ? "Liquidity added to the book" : "Order crossed the spread",
        subline: frame.remainingQuantity > 0
            ? "The book deepened at one or more price levels."
            : "The event consumed liquidity without leaving a resting remainder.",
    };
}

function midPrice(frame) {
    if (!frame || frame.bestBid < 0 || frame.bestAsk < 0) {
        return null;
    }

    return (frame.bestBid + frame.bestAsk) / 2;
}

function clearPlaybackTimer() {
    if (state.playTimer) {
        window.clearTimeout(state.playTimer);
        state.playTimer = null;
    }
}

function setPlaying(playing) {
    state.isPlaying = playing;
    elements.playToggle.textContent = playing ? "Pause" : "Play";
    elements.playToggle.setAttribute("aria-label", playing ? "Pause trace" : "Play trace");

    if (!playing) {
        clearPlaybackTimer();
        return;
    }

    scheduleNextFrame();
}

function scheduleNextFrame() {
    clearPlaybackTimer();
    if (!state.isPlaying || state.frames.length <= 1) {
        return;
    }

    const delay = Math.max(140, Math.round(680 / state.playbackRate));
    state.playTimer = window.setTimeout(() => {
        const nextIndex = state.currentIndex + 1;
        if (nextIndex >= state.frames.length) {
            setPlaying(false);
            return;
        }

        setFrame(nextIndex);
        scheduleNextFrame();
    }, delay);
}

function setStatus(message, isError = false) {
    elements.statusMessage.textContent = message;
    elements.statusMessage.style.color = isError ? "var(--ask)" : "";
}

function createBadge(label, className) {
    const badge = document.createElement("span");
    badge.className = `badge ${className}`.trim();
    badge.textContent = label;
    return badge;
}

function buildLadderRow(level, side, maxQuantity, highlight) {
    const row = document.createElement("article");
    row.className = `ladder-row ${side}${highlight ? " highlight" : ""}`;

    const bar = document.createElement("div");
    bar.className = "ladder-bar";
    const width = maxQuantity > 0 ? Math.max(8, Math.round((level.quantity / maxQuantity) * 100)) : 0;
    bar.style.width = `${width}%`;

    const main = document.createElement("div");
    main.className = "ladder-main";
    main.textContent = side === "ask" ? formatPrice(level.price) : formatNumber(level.quantity);

    const sub = document.createElement("div");
    sub.className = "ladder-sub";
    sub.textContent = side === "ask" ? formatNumber(level.quantity) : formatPrice(level.price);

    row.append(bar, main, sub);
    return row;
}

function renderLadder(container, levels, side, frame) {
    container.innerHTML = "";

    if (!levels || levels.length === 0) {
        const empty = document.createElement("div");
        empty.className = "ladder-empty";
        empty.textContent = side === "ask" ? "No asks at this moment." : "No bids at this moment.";
        container.append(empty);
        return;
    }

    const maxQuantity = Math.max(...levels.map((level) => level.quantity), 1);
    const touchedPrices = new Set(frame.trades.map((trade) => trade.price));

    levels.forEach((level) => {
        const highlight = touchedPrices.has(level.price);
        container.append(buildLadderRow(level, side, maxQuantity, highlight));
    });
}

function renderBadges(frame) {
    elements.eventBadges.innerHTML = "";
    elements.eventBadges.append(
        createBadge(actionLabel(frame.action), "action"),
        createBadge(sideLabel(frame.event.side), frame.event.side === "BUY" ? "bid" : "ask"),
        createBadge(typeLabel(frame.event.type), frame.event.type === "MARKET" ? "market" : "")
    );
}

function renderTrades(frame) {
    elements.tradeList.innerHTML = "";
    elements.tradeCount.textContent = `${frame.trades.length} trade${frame.trades.length === 1 ? "" : "s"}`;

    if (frame.trades.length === 0) {
        const empty = document.createElement("div");
        empty.className = "trade-empty";
        empty.textContent = "No executions were generated by this event.";
        elements.tradeList.append(empty);
        return;
    }

    frame.trades.forEach((trade) => {
        const item = document.createElement("article");
        item.className = "trade-item";
        item.innerHTML = `
            <div class="trade-topline">
                <strong>${formatNumber(trade.quantity)} @ ${formatPrice(trade.price)}</strong>
                <span>${formatNumber(trade.timestamp)}</span>
            </div>
            <div class="trade-subline">
                <span>Buy #${trade.buyOrderId}</span>
                <span>Sell #${trade.sellOrderId}</span>
            </div>
        `;
        elements.tradeList.append(item);
    });
}

function renderChart(svg, values, currentIndex, palette) {
    const width = 300;
    const height = 120;
    const safeValues = values.map((value) => (Number.isFinite(value) ? value : 0));
    const max = Math.max(...safeValues, 1);
    const min = Math.min(...safeValues, 0);
    const range = Math.max(max - min, 1);

    const points = safeValues.map((value, index) => {
        const x = safeValues.length === 1 ? width / 2 : (index / (safeValues.length - 1)) * width;
        const y = height - ((value - min) / range) * (height - 12) - 6;
        return [x, y];
    });

    const polyline = points.map(([x, y]) => `${x},${y}`).join(" ");
    const areaPath = [
        `M ${points[0]?.[0] ?? 0} ${height}`,
        ...points.map(([x, y]) => `L ${x} ${y}`),
        `L ${points[points.length - 1]?.[0] ?? width} ${height}`,
        "Z",
    ].join(" ");

    const currentPoint = points[currentIndex] ?? points[points.length - 1] ?? [0, height / 2];
    const [dotX, dotY] = currentPoint;

    svg.innerHTML = `
        <line class="chart-grid" x1="0" y1="${height - 1}" x2="${width}" y2="${height - 1}"></line>
        <path class="chart-area" d="${areaPath}" fill="${palette.fill}"></path>
        <polyline class="chart-line" points="${polyline}" stroke="${palette.stroke}"></polyline>
        <circle class="chart-dot" cx="${dotX}" cy="${dotY}" r="6" fill="${palette.stroke}"></circle>
    `;
}

function renderSummary(trace) {
    const summary = trace.summary ?? {};
    elements.summarySource.textContent = trace.sourceName ?? "Custom trace";
    elements.summaryFrames.textContent = `${formatNumber(state.frames.length)} captured frame${state.frames.length === 1 ? "" : "s"}`;
    elements.metricProcessed.textContent = formatNumber(summary.processedEvents ?? 0);
    elements.metricTrades.textContent = formatNumber(summary.generatedTrades ?? 0);
    elements.metricSpread.textContent = summary.spread >= 0 ? formatPrice(summary.spread) : "N/A";
    elements.metricRuntime.textContent = formatRuntime(summary.totalRuntimeNs ?? 0);
    elements.frameStep.textContent = `${trace.visualization.frameStep} event${trace.visualization.frameStep === 1 ? "" : "s"}`;
    elements.depthRange.textContent = `${trace.visualization.depth} level${trace.visualization.depth === 1 ? "" : "s"}`;
}

function renderFrame(frame) {
    const frameCount = state.frames.length;
    elements.frameCounter.textContent = `${state.currentIndex + 1} / ${frameCount}`;
    elements.timestampReadout.textContent = formatNumber(frame.timestamp);
    elements.timeline.max = String(Math.max(frameCount - 1, 0));
    elements.timeline.value = String(state.currentIndex);

    const mid = midPrice(frame);
    elements.midPrice.textContent = mid == null ? "N/A" : formatPrice(Math.round(mid));
    elements.spreadChip.textContent = `Spread: ${safeSpread(frame)}`;

    const narrative = stateNarrative(frame);
    elements.stateHeadline.textContent = narrative.headline;
    elements.stateSubline.textContent = narrative.subline;

    renderLadder(elements.askLadder, frame.asks, "ask", frame);
    renderLadder(elements.bidLadder, frame.bids, "bid", frame);
    renderBadges(frame);

    elements.eventHeadline.textContent = headlineForFrame(frame);
    elements.eventDescription.textContent = describeEvent(frame);
    elements.eventLatency.textContent = formatLatency(frame.latencyNs);
    elements.eventMatched.textContent = formatNumber(frame.matchedQuantity);
    elements.eventRemaining.textContent = formatNumber(frame.remainingQuantity);
    elements.eventActiveOrders.textContent = formatNumber(frame.activeOrders);

    renderTrades(frame);

    const spreads = state.frames.map((item) => (item.spread >= 0 ? item.spread / 100 : 0));
    const volume = state.frames.map((item) => item.cumulativeTradedQuantity ?? 0);

    elements.spreadChartValue.textContent = safeSpread(frame);
    elements.volumeChartValue.textContent = formatNumber(frame.cumulativeTradedQuantity ?? 0);

    renderChart(elements.spreadChart, spreads, state.currentIndex, {
        stroke: "#cb6149",
        fill: "rgba(203, 97, 73, 0.24)",
    });
    renderChart(elements.volumeChart, volume, state.currentIndex, {
        stroke: "#147b62",
        fill: "rgba(20, 123, 98, 0.22)",
    });
}

function validateTrace(trace) {
    if (!trace || !Array.isArray(trace.frames)) {
        throw new Error("Trace JSON is missing the frames array.");
    }

    if (!trace.visualization || typeof trace.visualization.depth !== "number") {
        throw new Error("Trace JSON is missing visualization metadata.");
    }
}

function loadTrace(trace, sourceLabel = trace.sourceName ?? "Custom trace") {
    validateTrace(trace);

    state.trace = {
        ...trace,
        sourceName: sourceLabel,
    };
    state.frames = trace.frames;
    state.currentIndex = 0;
    setPlaying(false);

    renderSummary(state.trace);

    if (state.frames.length === 0) {
        elements.stateHeadline.textContent = "No frames captured";
        elements.stateSubline.textContent = "Generate a trace with at least one captured event.";
        return;
    }

    setFrame(0);
}

function setFrame(index) {
    if (!state.frames.length) {
        return;
    }

    const boundedIndex = Math.max(0, Math.min(index, state.frames.length - 1));
    state.currentIndex = boundedIndex;
    renderFrame(state.frames[boundedIndex]);
}

async function loadSampleTrace() {
    setStatus("Loading bundled sample trace...");
    try {
        const response = await fetch(SAMPLE_TRACE_URL, { cache: "no-store" });
        if (!response.ok) {
            throw new Error(`Failed to fetch sample trace (${response.status})`);
        }

        const trace = await response.json();
        loadTrace(trace, trace.sourceName ?? "sample_orders.csv");
        setStatus("Bundled sample trace loaded.");
    } catch (error) {
        console.error(error);
        setStatus(
            "Sample trace could not be loaded automatically. Upload a local trace JSON or serve this folder over HTTP.",
            true
        );
    }
}

function handleFileUpload(event) {
    const [file] = event.target.files ?? [];
    if (!file) {
        return;
    }

    const reader = new FileReader();
    reader.onload = () => {
        try {
            const trace = JSON.parse(String(reader.result));
            loadTrace(trace, file.name);
            setStatus(`Loaded local trace: ${file.name}`);
        } catch (error) {
            console.error(error);
            setStatus("That file is not a valid visualization trace JSON.", true);
        }
    };
    reader.onerror = () => {
        setStatus("Failed to read the selected file.", true);
    };
    reader.readAsText(file);
}

elements.loadSample.addEventListener("click", () => {
    loadSampleTrace();
});

elements.traceUpload.addEventListener("change", handleFileUpload);

elements.timeline.addEventListener("input", (event) => {
    setPlaying(false);
    setFrame(Number(event.target.value));
});

elements.stepBack.addEventListener("click", () => {
    setPlaying(false);
    setFrame(state.currentIndex - 1);
});

elements.stepForward.addEventListener("click", () => {
    setPlaying(false);
    setFrame(state.currentIndex + 1);
});

elements.playToggle.addEventListener("click", () => {
    if (!state.frames.length) {
        return;
    }

    setPlaying(!state.isPlaying);
});

elements.speedSelect.addEventListener("change", (event) => {
    state.playbackRate = Number(event.target.value);
    if (state.isPlaying) {
        scheduleNextFrame();
    }
});

document.addEventListener("visibilitychange", () => {
    if (document.hidden) {
        setPlaying(false);
    }
});

loadSampleTrace();
