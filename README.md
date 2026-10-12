# QuantXCore

**QuantXCore** is a C++20 project focused on cryptocurrency market data processing, trading strategy development, risk management, and trade execution.

The project is being developed as a modular trading and quantitative analysis system. Its architecture separates market data, strategy logic, risk controls, execution, portfolio management, and performance analytics.

> **Development status:** Work in progress. APIs, features, and system behavior may change as development continues. The project is not presented as a production-ready trading system.

## Current Architecture

The repository is organized into the following modules:

| Module | Responsibility |
|---|---|
| `data` | Market data interfaces, Binance connectivity, CSV tick parsing, order book handling, and Redis streaming |
| `alpha` | Trading signals, strategy interfaces, moving-average crossover, RSI, and additional strategy implementations |
| `risk` | Risk checks, position sizing, risk limits, and position risk tracking |
| `execution` | Live and backtest engines, order management, execution clients, and PnL tracking |
| `portfolio` | Portfolio state and position tracking |
| `analytics` | Trading performance analytics |
| `persistence` | Database management and persistence-related functionality |
| `utils` | Configuration loading and shared utilities |

The codebase also includes unit tests for selected components.

## Technology Stack

- **Language:** C++20
- **Build system:** CMake
- **Market data and connectivity:** Binance REST/streaming interfaces, CURL
- **Networking and security dependencies:** Boost, OpenSSL, Threads
- **Serialization:** `nlohmann/json`
- **Data storage and streaming dependencies:** SQLite, Redis/hiredis
- **Testing:** CTest and the project's configured unit-test framework

## Project Structure

```text
QuantXCore/
├── cmake/
├── config/
├── include/
│   └── QuantXCore/
│       ├── alpha/
│       ├── analytics/
│       ├── data/
│       ├── execution/
│       ├── persistence/
│       ├── portfolio/
│       ├── risk/
│       └── utils/
├── src/
│   ├── alpha/
│   ├── analytics/
│   ├── data/
│   ├── execution/
│   ├── portfolio/
│   └── risk/
└── tests/
    └── unit/
```

## Build

### Prerequisites

- A C++20-compatible compiler
- CMake 3.20 or newer
- The required development libraries listed in the root `CMakeLists.txt`
- Git

Some dependencies are fetched by CMake during configuration, so network access may be required.

### Configure and compile

```bash
git clone https://github.com/VardanKocharyan/QuantXCore.git
cd QuantXCore

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
```

### Run tests

```bash
ctest --test-dir build --output-on-failure
```

Some tests may require external services or runtime configuration. Check the relevant test setup before interpreting skipped or unavailable integration tests.

## Configuration and Credentials

Use `.env.example` as a reference for local environment configuration, where applicable. Keep local `.env` files, API credentials, and other secrets out of version control.

Do not commit live exchange credentials or use real funds without independently reviewing the system's behavior and risk controls.

## Scope and Roadmap

Development is ongoing. Areas of continued work include:

- Improving market data handling and synchronization
- Developing and evaluating trading strategies
- Refining risk controls and execution behavior
- Improving backtesting and performance analysis
- Expanding automated testing and documentation

This list describes development areas, not a guarantee that every item is complete or production-ready.

## Disclaimer

QuantXCore is a software development and quantitative analysis project. It is not financial advice and does not guarantee trading performance or profitability. Cryptocurrency trading involves substantial risk.
