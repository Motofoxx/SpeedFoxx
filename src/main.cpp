/**
 * ============================================================================
 * @file        main.cpp
 * @brief       Motorcycle Telemetry Dashboard & Drag Racing Simulator
 * @version     1.1.0
 * @author      Motofoxx
 * @date        2026-05-26
 *
 * @versioning
 * - Major: breaking changes and new platform support
 * - Minor: new features and UI improvements
 * - Patch: small fixes and refinements
 *
 * @description
 * An embedded telemetric display system designed for the ESP32 platform.
 * This firmware acts as a bridge between physical vehicle inputs and a
 * high-fidelity digital interface. It features a non-blocking physics
 * engine for drag race simulation, an interactive hardware UI via an
 * SSD1306 OLED, and an asynchronous web portal for live data visualization
 * and remote configuration.
 *
 * @hardware
 * - MCU:       ESP32-WROOM-32E development board
 * - Display:   SSD1306 OLED 128x64 (I2C: SDA=21, SCL=22)
 * - Inputs:    2x Momentary Push Buttons (Active LOW)
 * - Isolation: 4x 4N35 optocouplers for speed, RPM, neutral, kickstand
 * - Output:    2N2222 transistor for corrected speed pulse drive
 *
 * @pin mapping
 * - Speed input: GPIO14 (optocoupler output, active low)
 * - RPM input:   GPIO27 (optocoupler output, active low)
 * - Neutral:     GPIO32 (optocoupler output, active low)
 * - Kickstand:   GPIO33 (optocoupler output, active low)
 * - Speed out:   GPIO26 (corrected speed pulse output)
 * - Button top:  GPIO2  (menu unlock/edit)
 * - Button right:GPIO0  (page/selection nav)
 *
 * @dependencies
 * - Arduino.h, Wire.h, WiFi.h, WebServer.h (Built-in)
 * - Adafruit GFX Library
 * - Adafruit SSD1306 Library
 *
 * @notes
 * - Uses `INPUT_PULLUP` for optocoupler outputs.
 * - Corrected speed output is generated with ESP32 LEDC tone generation.
 * - Designed to sit inline between bike speed sensor and factory cluster.
 * ============================================================================
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WebServer.h>

#define SCREEN_WIDTH 128 
#define SCREEN_HEIGHT 64 
#define OLED_RESET     -1 
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

const char* ssid = "SpeedFoxx_Network";
const char* password = "motorcycle123";
WebServer server(80);

const char* FIRMWARE_VERSION = "1.1.1";
const char* FIRMWARE_CHANGELOG = R"history([
  {"version":"1.1.1","notes":"Fixed web portal rendering, added version display, and centralized large-gear styling."},
  {"version":"1.1.0","notes":"Initial motorcycle gear dashboard, race pairing, and portal UI."}
])history";
const int PIN_SPEED_IN = 14;
const int PIN_RPM_IN = 27;
const int PIN_NEUTRAL_IN = 32;
const int PIN_KICKSTAND_IN = 33;
const int PIN_SPEED_OUT = 26;

const int BUTTON_TOP = 2;   // top or left button depending on device orientation
const int BUTTON_RIGHT = 0; // bottom or right button depending on device orientation

const int SPEED_PULSES_PER_REV = 4;
const int RPM_PULSES_PER_REV = 2;
const bool INPUT_ACTIVE_LOW = true;
const bool OUTPUT_ACTIVE_LOW = false;

const float PRIMARY_DRIVE_RATIO = 2.111;
const float STOCK_TIRE_CIRCUMFERENCE_MILES = 0.001253;
float currentTireCircumferenceMiles = STOCK_TIRE_CIRCUMFERENCE_MILES;

const int SPEED_OUT_CHANNEL = 0;

volatile unsigned long lastSpeedPulseMicros = 0;
volatile unsigned long lastSpeedIntervalMicros = 0;
volatile unsigned long lastRpmPulseMicros = 0;
volatile unsigned long lastRpmIntervalMicros = 0;

int currentPage = 0;
const int TOTAL_PAGES = 12; 
unsigned long lastButtonCheck = 0;

// --- Advanced Menu Lock & State Variables ---
bool isMenuUnlocked = false;       
bool isEditModeActive = false;      
unsigned long buttonHoldStartTime = 0; 
bool isHoldingUnlockButton = false;   
unsigned long lastActivityTime = 0; 

// Button Release Tracking Flags (State Locks)
bool topButtonWasReleased = true;
bool rightButtonWasReleased = true;

// Interactive Navigation Array Indices
int odometerSelectRow = 0; 
int speedOffsetSelectIdx = 0; 

// Extended Timeouts
const unsigned long UNLOCK_HOLD_TIME = 2000;   
const unsigned long EDIT_MODE_TIMEOUT = 10000; 
const unsigned long PAGE_HOME_TIMEOUT = 300000;  
const unsigned long MASTER_LOCK_TIMEOUT = 600000; 

// Accidental click cushion variables
unsigned long lastDoublePressTime = 0;
const unsigned long DEBOUNCE_CUSHION = 500; 

// --- Gearing & Core Telemetry Variables ---
int currentRPM = 0;     
int currentMPH = 0;
int currentGearNum = 0;    
String currentGear = "N";
int userShiftPoint = 14500; 
int lightFlashDelay = 50; 
int coolantTempF = 72; 

// Remote/Hardware Interactive State Controls
bool isSimulationActive = false; 
enum TelemetrySource { SOURCE_SIMULATOR, SOURCE_BIKE_INPUTS };
TelemetrySource telemetrySource = SOURCE_SIMULATOR;

// Lifetime Peak Records Registry
int maxRecordedSpeed = 0;
int maxRecordedRPM = 0;

// --- Hardware Diagnostics Variables ---
String optoRPMStatus = "AWAIT_SIG"; 
String optoSPDStatus = "AWAIT_SIG"; 
String optoNeutralStatus = "AWAIT_SIG";
String optoKickstandStatus = "AWAIT_SIG";

// --- Page 3: Advanced Drag Racing State Machine Variables ---
enum TimerState { READY_TO_LAUNCH, RACING, RUN_COMPLETE };
TimerState currentTimerState = READY_TO_LAUNCH;

unsigned long runStartTime = 0;
unsigned long runFinishTime = 0; 
const unsigned long TICKET_DISPLAY_DURATION = 60000; 

// Live Run Registers
float time0To60 = 0.0;
float time0To100 = 0.0;
float timeQuarterMile = 0.0;
int speedQuarterMile = 0;
float dragDistanceFeet = 0.0;
bool achieved60 = false;
bool achieved100 = false;

// All-Time Records Registry
float best0To60 = 3.20;
float best0To100 = 6.40;
float bestQuarterMile = 10.85;
int bestSpeedQuarterMile = 134;

// --- Maintenance & Custom Odometers Registers ---
float totalOdometer = 124.5;
float tripA = 0.0;
float tripB = 0.0;
float engineHours = 0.0;

// Advanced Interactive Oil Registry Tracker
int oilLifePercent = 100;
float milesOnCurrentOil = 0.0;
float hoursOnCurrentOil = 0.0;

// --- Drag Strip Simulator Physics Core Engine ---
enum DragState { DRAG_IDLE, DRAG_STAGE_ROLL, DRAG_BURNOUT, DRAG_PRE_LAUNCH, DRAG_RACE_WOT, DRAG_COMPLETE };
DragState dragSimState = DRAG_IDLE;
unsigned long simStateTimer = 0;
unsigned long lastSimUpdate = 0;
bool flashInverted = false;

// Sprocket Physics Profile Matrices
struct SprocketSetup {
  int frontTeeth;
  int rearTeeth;
  float finalDriveRatio;
  float quarterMileTime;
  int quarterMileSpeed;
  float run0To60;
  float run0To100;
  bool recorded;
};

SprocketSetup simulationSetups[4] = {
  {15, 43, 43.0 / 15.0, 0.0, 0, 0.0, 0.0, false}, 
  {15, 45, 45.0 / 15.0, 0.0, 0, 0.0, 0.0, false}, 
  {16, 43, 43.0 / 16.0, 0.0, 0, 0.0, 0.0, false}, 
  {16, 45, 45.0 / 16.0, 0.0, 0, 0.0, 0.0, false}  
};
const int STOCK_FRONT_TEETH = 16;
const int STOCK_REAR_TEETH = 43;
int currentFrontTeeth = 15;
int currentRearTeeth = 45;
int currentSetupIndex = 1;    
int userRestoredSetupIdx = 1; 
bool isDemoModeActive = false; 

// --- Race Pairing & Lobby State ---
enum RaceState { RACE_IDLE, RACE_HOSTING, RACE_PAIRED, RACE_ACTIVE };
RaceState raceState = RACE_IDLE;
String raceHostToken = "";
String racePartnerIp = "";
unsigned long raceStartTime = 0;

// --- HTML Core Portal Source (Saved in Flash Memory) ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>SpeedFoxx Portal</title>
    <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no, viewport-fit=cover">
    
    <meta name="apple-mobile-web-app-capable" content="yes">
    <meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
    <meta name="mobile-web-app-capable" content="yes">
    
    <style>
        body { font-family: -apple-system, BlinkMacSystemFont, sans-serif; background: #050507; color: #e2e2e7; margin: 0; padding: 10px; text-align: center; -webkit-user-select: none; overflow: hidden; }
        .dashboard { max-width: 850px; margin: 0 auto; background: #0f0f14; padding: 15px; border-radius: 20px; box-shadow: 0 12px 36px rgba(0,0,0,0.9); border: 1px solid #1c1c24; display: flex; flex-direction: column; height: calc(100vh - 20px); box-sizing: border-box; transition: background-color 0.05s ease; position: relative; padding-bottom: 68px; }
        
        .shift-flash-active { background-color: #5a0000 !important; }
        
        .status-bar { display: flex; justify-content: space-between; padding-bottom: 8px; border-bottom: 1px solid #242430; font-size: 11px; color: #007aff; font-weight: 700; letter-spacing: 1px; text-transform: uppercase; }
        .status-bar span { color: #8e8e93; font-weight: 500; }
        
        .content-area { flex: 1; display: flex; justify-content: center; align-items: center; margin: 10px 0; overflow: hidden; position: relative; gap: 0; }
        
        .gauge-container { width: 100%; height: 100%; display: flex; justify-content: center; align-items: center; background: #07070a; border-radius: 16px; border: 1px solid #14141c; box-sizing: border-box; padding: 5px; transition: transform 0.3s ease; position: relative; }
        canvas { max-height: 100%; width: auto; aspect-ratio: 1/1; }
        
        .sub-page { display: none; width: 100%; height: 100%; text-align: left; box-sizing: border-box; padding: 10px 20px; overflow-y: auto; }
        .sub-page h2 { color: #007aff; font-size: 20px; margin-top: 0; border-bottom: 1px solid #242430; padding-bottom: 8px; font-weight: 700; text-transform: uppercase; }
        .data-table { width: 100%; border-collapse: collapse; }
        .data-table td { padding: 10px 0; border-bottom: 1px solid #1c1c24; font-size: 15px; }
        .data-table td:last-child { text-align: right; font-weight: bold; color: #fff; }
        
        .control-panel-wrapper { display: flex; flex-direction: column; align-items: stretch; justify-content: flex-start; height: 100%; gap: 15px; overflow-y: auto; padding-right: 5px; }
        .control-section { background: #14141c; padding: 15px; border-radius: 12px; border: 1px solid #242430; }
        .control-row { display: flex; justify-content: space-between; align-items: center; margin-bottom: 10px; }
        .control-row:last-child { margin-bottom: 0; }
        .control-label-text { font-size: 14px; font-weight: 600; color: #fff; }
        .interactive-btn { background: #007aff; color: #fff; border: none; padding: 8px 16px; border-radius: 8px; font-weight: 600; cursor: pointer; font-size: 13px; }
        .interactive-btn:active { background: #0056b3; }
        .danger-btn { background: #ff3b30; }
        .danger-btn:active { background: #c62828; }
        
        .layout-grid { display: grid; grid-template-columns: repeat(3, 1fr); gap: 10px; margin-top: 10px; }
        .layout-card { background: #1c1c24; border: 2px solid #2c2c3c; border-radius: 10px; padding: 12px; text-align: center; cursor: pointer; color: #aeaeae; font-weight: 600; font-size: 13px; transition: all 0.2s ease; }
        .layout-card.selected { border-color: #007aff; background: rgba(0, 122, 255, 0.1); color: #fff; }
        .cluster-group-title { font-size: 11px; font-weight:700; color:#8e8e93; text-transform:uppercase; margin: 4px 0 8px; letter-spacing: 0.5px; }
        .layout-tag { display: block; margin-top: 5px; color: #8e8e93; font-size: 10px; font-weight: 800; text-transform: uppercase; }
        
        .val-display { font-family: monospace; font-size: 16px; font-weight: 700; color: #34c759; background: #07070a; padding: 4px 10px; border-radius: 6px; border: 1px solid #1c1c24; min-width: 60px; text-align: center; }
        .adjust-box { display: flex; align-items: center; gap: 8px; }
        .step-btn { background: #2c2c3c; color: #fff; border: 1px solid #3a3a4c; width: 28px; height: 28px; border-radius: 6px; cursor: pointer; font-weight: bold; font-size: 16px; display: flex; align-items: center; justify-content: center; }
        .step-btn:active { background: #44445a; }
        .source-toggle { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; }
        .mode-btn { background: #1c1c24; color: #aeaeae; border: 1px solid #2c2c3c; min-height: 38px; border-radius: 8px; font-weight: 700; cursor: pointer; }
        .mode-btn.active { background: #007aff; border-color: #007aff; color: #fff; box-shadow: 0 0 10px rgba(0,122,255,0.35); }
        .select-row { display: flex; align-items: center; gap: 10px; }
        .select-row select { flex: 1; min-width: 0; background: #07070a; color: #fff; border: 1px solid #2c2c3c; border-radius: 8px; padding: 9px 10px; font-weight: 700; font-size: 13px; }
        .status-message { color: #34c759; font-size: 12px; font-weight: 800; min-height: 16px; margin-top: 10px; }
        .gear-tracker { position: absolute; top: 8px; left: 50%; transform: translateX(-50%); display: flex; justify-content: center; gap: 8px; flex-wrap: nowrap; overflow-x: auto; -webkit-overflow-scrolling: touch; z-index: 4; }
        .gear-dot { width: 28px; height: 28px; border-radius: 50%; display: inline-flex; align-items: center; justify-content: center; font-size: 11px; font-weight: 800; border: 2px solid #242430; color: #8e8e93; background: #101018; }
        .gear-dot.active { box-shadow: 0 0 8px rgba(255,255,255,0.18); }
        .gear-dot.active.neutral { background: #34c759; color: #071007; border-color: #34c759; }
        .gear-dot.active.yellow { background: #ffd60a; color: #101010; border-color: #b08900; }
        .gear-dot.active.red { background: #ff3b30; color: #071007; border-color: #bf1c24; }
        .race-status-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; }
        .race-card { background: #111118; border: 1px solid #242430; border-radius: 12px; padding: 15px; }
        .race-card h3 { margin: 0 0 12px; font-size: 14px; color: #8e8e93; text-transform: uppercase; letter-spacing: 0.5px; }
        .race-card .race-line { display: flex; justify-content: space-between; align-items: center; margin-bottom: 10px; font-size: 13px; }
        .race-card .race-line span:last-child { color: #fff; font-weight: 700; }
        .race-input-row { display: flex; flex-wrap: wrap; gap: 10px; margin-top: 10px; }
        .race-input-row input { flex: 1; min-width: 180px; background: #07070a; border: 1px solid #242430; border-radius: 10px; color: #fff; padding: 10px; }
        .race-input-row button { flex: 1; min-width: 120px; }
        .race-hint { color: #8e8e93; font-size: 12px; margin-top: 8px; }
        .metric-grid { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 10px; }
        .metric-cell { background: #07070a; border: 1px solid #242430; border-radius: 8px; padding: 10px; }
        .metric-label { color: #8e8e93; font-size: 10px; font-weight: 700; text-transform: uppercase; letter-spacing: 0.5px; margin-bottom: 5px; }
        .metric-value { color: #fff; font-size: 18px; font-weight: 800; }
        .metric-value.good { color: #34c759; }
        .metric-value.warn { color: #ffcc00; }
        .channel-grid { display: grid; grid-template-columns: repeat(4, minmax(0, 1fr)); gap: 8px; }
        .channel-card { background: #07070a; border: 1px solid #242430; border-radius: 8px; padding: 9px; text-align: center; }
        .channel-name { color: #8e8e93; font-size: 10px; text-transform: uppercase; font-weight: 800; margin-bottom: 5px; }
        .channel-state { color: #34c759; font-size: 11px; font-weight: 800; overflow-wrap: anywhere; }
        @media (max-width: 560px) {
            .metric-grid { grid-template-columns: 1fr; }
            .channel-grid { grid-template-columns: repeat(2, minmax(0, 1fr)); }
        }

        .switch { position: relative; display: inline-block; width: 50px; height: 28px; }
        .switch input { opacity: 0; width: 0; height: 0; }
        .slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #3a3a3c; transition: .4s; border-radius: 28px; }
        .slider:before { position: absolute; content: ""; height: 20px; width: 20px; left: 4px; bottom: 4px; background-color: white; transition: .4s; border-radius: 50%; }
        input:checked + .slider { background-color: #34c759; }
        input:checked + .slider:before { transform: translateX(22px); }
        
        .nav-bar { display: flex; position: absolute; bottom: 12px; left: 20px; right: 20px; gap: 8px; scrollbar-width: none; min-height: 44px; overflow-x: auto; -webkit-overflow-scrolling: touch; justify-content: flex-start; }
        .nav-bar::-webkit-scrollbar { display: none; }
        .nav-btn { background: #1c1c24; color: #aeaeae; border: 1px solid #2c2c3c; padding: 0 14px; border-radius: 30px; font-size: 12px; font-weight: 600; cursor: pointer; white-space: nowrap; height: 36px; display: flex; align-items: center; justify-content: center; flex: 0 0 auto; min-width: 88px; }
        .nav-btn.active { background: #007aff; color: #fff; border-color: #007aff; box-shadow: 0 0 10px rgba(0,122,255,0.4); }
        .nav-center { left: 50% !important; transform: translateX(-50%) !important; justify-content: center !important; width: calc(100% - 40px) !important; }
        .version-badge { font-size: 10px; font-weight: 700; letter-spacing: 1px; color: #8e8e93; text-transform: uppercase; align-self: center; }
        .version-notes { color: #8e8e93; font-size: 12px; line-height: 1.4; margin-top: 10px; }
    </style>
</head>
<body>
    <div class="dashboard" id="master-panel">
        <div class="status-bar">
            <div>SPEEDFOXX SF WEB PORTAL</div>
            <div id="sprocket-banner">SPROCKET: <span>-- / --</span></div>
            <div id="version-badge">FW: 1.1.1</div>
        </div>
        
        <div class="content-area">
            <div id="page-dash" class="gauge-container">
                <canvas id="lfaGauge" width="340" height="340"></canvas>
                <div id="gear-tracker" class="gear-tracker"></div>

            </div>

            <div id="page-control" class="sub-page">
                <div class="control-panel-wrapper">
                    <h2>SF Command & Hardware</h2>

                    <div class="control-section">
                        <div style="font-size: 11px; font-weight:700; color:#8e8e93; text-transform:uppercase; margin-bottom:10px;">Telemetry Data Source</div>
                        <div class="source-toggle">
                            <button class="mode-btn active" id="source-sim" onclick="setDataSource('sim')">Simulator</button>
                            <button class="mode-btn" id="source-bike" onclick="setDataSource('bike')">Bike Inputs</button>
                        </div>
                    </div>
                    
                    <div class="control-section">
                        <div class="control-row">
                            <span class="control-label-text">Simulator Engine</span>
                            <label class="switch">
                                <input type="checkbox" id="simToggle" onchange="toggleSimCore(this)">
                                <span class="slider"></span>
                            </label>
                        </div>
                    </div>

                    <div class="control-section">
                        <div style="font-size: 11px; font-weight:700; color:#8e8e93; text-transform:uppercase; margin-bottom:10px;">Hardware Tuning & Controls</div>
                        
                        <div class="control-row">
                            <span class="control-label-text">Shift Limit Indicator (RPM)</span>
                            <div class="adjust-box">
                                <button class="step-btn" onclick="adjustSetting('shift', -100)">-</button>
                                <span class="val-display" id="web-shift">--</span>
                                <button class="step-btn" onclick="adjustSetting('shift', 100)">+</button>
                            </div>
                        </div>

                        <div class="control-row">
                            <span class="control-label-text">Shift Light Refresh Rate (ms)</span>
                            <div class="adjust-box">
                                <button class="step-btn" onclick="adjustSetting('flash', -5)">-</button>
                                <span class="val-display" id="web-flash">--</span>
                                <button class="step-btn" onclick="adjustSetting('flash', 5)">+</button>
                            </div>
                        </div>
                    </div>

                    <div class="control-section">
                        <div style="font-size: 11px; font-weight:700; color:#8e8e93; text-transform:uppercase; margin-bottom:10px;">Hardware Signal Config</div>
                        <div class="metric-grid">
                            <div class="metric-cell">
                                <div class="metric-label">Speed Input Pin</div>
                                <div class="metric-value" id="pin-speed-in">--</div>
                            </div>
                            <div class="metric-cell">
                                <div class="metric-label">RPM Input Pin</div>
                                <div class="metric-value" id="pin-rpm-in">--</div>
                            </div>
                            <div class="metric-cell">
                                <div class="metric-label">Neutral Input Pin</div>
                                <div class="metric-value" id="pin-neutral-in">--</div>
                            </div>
                            <div class="metric-cell">
                                <div class="metric-label">Kickstand Input Pin</div>
                                <div class="metric-value" id="pin-kickstand-in">--</div>
                            </div>
                            <div class="metric-cell">
                                <div class="metric-label">Corrected Speed Output</div>
                                <div class="metric-value" id="pin-speed-out">--</div>
                            </div>
                            <div class="metric-cell">
                                <div class="metric-label">Input Polarity</div>
                                <div class="metric-value" id="input-polarity">--</div>
                            </div>
                            <div class="metric-cell">
                                <div class="metric-label">Speed Pulses / Rev</div>
                                <div class="metric-value" id="speed-ppr">--</div>
                            </div>
                            <div class="metric-cell">
                                <div class="metric-label">RPM Pulses / Rev</div>
                                <div class="metric-value" id="rpm-ppr">--</div>
                            </div>
                        </div>
                    </div>
                </div>
                  <div class="control-section">
                    <div style="font-size: 11px; font-weight:700; color:#8e8e93; text-transform:uppercase; margin-bottom:10px;">Portal Settings</div>
                    <div class="control-row">
                      <span class="control-label-text">Firmware Version</span>
                      <span class="val-display" id="current-version">1.1.1</span>
                    </div>
                    <div id="version-notes" class="version-notes">Latest firmware notes will appear here.</div>
                    <div class="control-row">
                      <span class="control-label-text">Center Nav Bar on Large Displays</span>
                      <label class="switch"><input type="checkbox" id="setting-nav-center" onchange="toggleNavCenter(this)"><span class="slider"></span></label>
                    </div>
                    <div class="control-row">
                      <span class="control-label-text">Run Gear Simulation</span>
                      <button class="interactive-btn" onclick="runGearSimulation()">Run Simulation</button>
                    </div>
                  </div>
            </div>

            <div id="page-sprockets" class="sub-page">
                <div class="control-panel-wrapper">
                    <h2>SF Sprockets & Ratios</h2>

                    <div class="control-section">
                        <div style="font-size: 11px; font-weight:700; color:#8e8e93; text-transform:uppercase; margin-bottom:10px;">Current Bike Setup</div>
                        <div class="select-row">
                            <select id="sprocket-select" onchange="markSprocketSelectionDirty()">
                                <option value="0">15T / 43T</option>
                                <option value="1">15T / 45T (-1 / +2)</option>
                                <option value="2">16T / 43T Stock</option>
                                <option value="3">16T / 45T</option>
                            </select>
                            <button class="interactive-btn" onclick="applySprocketSetup()">Apply</button>
                        </div>
                        <div class="status-message" id="sprocket-status"></div>
                    </div>

                    <div class="control-section">
                        <div class="metric-grid">
                            <div class="metric-cell">
                                <div class="metric-label">Stock Setup</div>
                                <div class="metric-value" id="stock-setup">--T / --T</div>
                            </div>
                            <div class="metric-cell">
                                <div class="metric-label">Current Setup</div>
                                <div class="metric-value warn" id="current-setup">--T / --T</div>
                            </div>
                            <div class="metric-cell">
                                <div class="metric-label">Stock Final Drive</div>
                                <div class="metric-value" id="stock-final">--</div>
                            </div>
                            <div class="metric-cell">
                                <div class="metric-label">Current Final Drive</div>
                                <div class="metric-value" id="current-final">--</div>
                            </div>
                            <div class="metric-cell">
                                <div class="metric-label">Correction Factor</div>
                                <div class="metric-value good" id="correction-factor">--</div>
                            </div>
                            <div class="metric-cell">
                                <div class="metric-label">Displayed 100 MPH Becomes</div>
                                <div class="metric-value good" id="corrected-100">-- MPH</div>
                            </div>
                        </div>
                    </div>

                    <div class="control-section">
                        <div style="font-size: 11px; font-weight:700; color:#8e8e93; text-transform:uppercase; margin-bottom:10px;">Input Channel Status</div>
                        <div class="channel-grid">
                            <div class="channel-card">
                                <div class="channel-name">Speed</div>
                                <div class="channel-state" id="ch-speed">--</div>
                            </div>
                            <div class="channel-card">
                                <div class="channel-name">RPM</div>
                                <div class="channel-state" id="ch-rpm">--</div>
                            </div>
                            <div class="channel-card">
                                <div class="channel-name">Neutral</div>
                                <div class="channel-state" id="ch-neutral">--</div>
                            </div>
                            <div class="channel-card">
                                <div class="channel-name">Kickstand</div>
                                <div class="channel-state" id="ch-kickstand">--</div>
                            </div>
                        </div>
                    </div>
                </div>
            </div>

            <div id="page-clusters" class="sub-page">
                <div class="control-panel-wrapper">
                    <h2>SF Dashboard Clusters</h2>

                    <div class="control-section">
                        <div class="cluster-group-title">Detailed Replicas</div>
                        <div class="layout-grid">
                            <div class="layout-card selected" id="lay-cbr600rr-detail" onclick="setLayout('cbr600rr-detail')">CBR600RR<span class="layout-tag">Detailed</span></div>
                            <div class="layout-card" id="lay-ducati800-detail" onclick="setLayout('ducati800-detail')">800SS<span class="layout-tag">Detailed</span></div>
                            <div class="layout-card" id="lay-gsxr600-detail" onclick="setLayout('gsxr600-detail')">GSX-R600<span class="layout-tag">Detailed</span></div>
                        </div>
                    </div>

                    <div class="control-section">
                        <div class="cluster-group-title">Generic / Extras</div>
                        <div class="layout-grid">
                            <div class="layout-card" id="lay-gsxr25" onclick="setLayout('gsxr25')">'25 GSX-R<span class="layout-tag">Generic</span></div>
                            <div class="layout-card" id="lay-zx10r26" onclick="setLayout('zx10r26')">'26 ZX-10R<span class="layout-tag">Generic</span></div>
                            <div class="layout-card" id="lay-lfa" onclick="setLayout('lfa')">LFA Pod<span class="layout-tag">Extra</span></div>
                            <div class="layout-card" id="lay-s2000" onclick="setLayout('s2000')">S2000 Peak<span class="layout-tag">Extra</span></div>
                            <div class="layout-card" id="lay-ducati" onclick="setLayout('ducati')">Panigale V4<span class="layout-tag">Generic</span></div>
                            <div class="layout-card" id="lay-cbr05" onclick="setLayout('cbr05')">CBR600<span class="layout-tag">Generic</span></div>
                        </div>
                    </div>
                </div>
            </div>

            <div id="page-perf" class="sub-page">
                <h2>Performance Timer Logs</h2>
                <table class="data-table">
                    <tr><td>0-60 MPH Velocity Sprint</td><td id="p-60">0.00 s</td></tr>
                    <tr><td>0-100 MPH Velocity Sprint</td><td id="p-100">0.00 s</td></tr>
                    <tr><td>Elapsed 1/4 Mile Strip Time</td><td id="p-quarter">0.00 s</td></tr>
                    <tr><td>Terminal Trap Speed Finish</td><td id="p-trap">0 MPH</td></tr>
                </table>
            </div>

            <div id="page-records" class="sub-page">
                <h2>Lifetime Record Registry</h2>
                <table class="data-table">
                    <tr><td>Peak Historical Velocity</td><td id="r-speed">0 MPH</td></tr>
                    <tr><td>Peak Engine Speed Reached</td><td id="r-rpm">0 RPM</td></tr>
                    <tr><td>All-Time Best 0-60 Sprint</td><td id="r-60">0.00 s</td></tr>
                    <tr><td>All-Time Best 1/4 Mile Run</td><td id="r-quarter">0.00 s</td></tr>
                </table>
            </div>

            <div id="page-race" class="sub-page">
                <div class="control-panel-wrapper">
                    <h2>Race Lobby</h2>

                    <div class="control-section race-card">
                        <h3>Local Device</h3>
                        <div class="race-line"><span>Device IP</span><span id="race-device-ip">--</span></div>
                        <div class="race-line"><span>Local Role</span><span id="race-local-role">Solo</span></div>
                        <div class="race-line"><span>Current State</span><span id="race-status">Idle</span></div>
                        <div class="race-line"><span>Host Token</span><span id="race-origin-token">—</span></div>
                        <div class="race-line"><span>Partner</span><span id="race-partner-ip">None</span></div>
                    </div>

                    <div class="control-section race-card">
                        <h3>Host Controls</h3>
                        <div class="race-input-row">
                            <button class="interactive-btn" onclick="createRaceSession()">Create Race Host</button>
                            <button class="interactive-btn" onclick="startRaceSession()">Start Race</button>
                            <button class="interactive-btn danger-btn" onclick="cancelRaceSession()">Cancel Race</button>
                        </div>
                        <div class="race-hint">Use these controls when you want this SpeedFoxx to act as the host device.</div>
                    </div>

                    <div class="control-section race-card">
                        <h3>Join a Race</h3>
                        <div class="race-input-row">
                            <input type="text" id="race-host-ip" placeholder="Host IP address">
                            <input type="text" id="race-token-input" placeholder="Host race token">
                        </div>
                        <div class="race-input-row">
                            <button class="interactive-btn" onclick="joinRaceHost()">Join Host</button>
                            <button class="interactive-btn" onclick="queryHostRaceStatus()">Check Host</button>
                        </div>
                        <div class="race-hint">Enter the host gateway address and token from the host device portal.</div>
                    </div>
                </div>
            </div>

            <div id="page-service" class="sub-page">
                <h2>Maintenance Logs</h2>
                <table class="data-table">
                    <tr><td>Total Hours Run</td><td id="s-hours">0.0000 hrs</td></tr>
                    <tr><td>Oil Life Index Remaining</td><td id="s-oil">100%</td></tr>
                    <tr><td>Engine Hours Since Change</td><td id="s-oil-hours">0.0000 hrs</td></tr>
                    <tr><td>Odometer Since Last Change</td><td id="s-oil-miles">0.00 mi</td></tr>
                    <tr><td>Trip Meter A Register</td><td id="s-tripa">0.00 mi</td></tr>
                    <tr><td>Trip Meter B Register</td><td id="s-tripb">0.00 mi</td></tr>
                </table>
                <div class="control-section" style="margin-top:15px;">
                    <div style="font-size: 11px; font-weight:700; color:#8e8e93; text-transform:uppercase; margin-bottom:10px;">Maintenance Resets</div>
                    <div class="control-row">
                        <span class="control-label-text">Engine Oil Life Metrics</span>
                        <button class="interactive-btn danger-btn" onclick="resetOilLifeRemote()">Reset Oil Life</button>
                    </div>
                    <div class="control-row">
                        <span class="control-label-text">Trip Meter A Register</span>
                        <button class="interactive-btn danger-btn" onclick="resetTripRemote('A')">Reset Trip A</button>
                    </div>
                    <div class="control-row">
                        <span class="control-label-text">Trip Meter B Register</span>
                        <button class="interactive-btn danger-btn" onclick="resetTripRemote('B')">Reset Trip B</button>
                    </div>
                </div>
            </div>
        </div>

        <div class="nav-bar">
            <button class="nav-btn active" onclick="switchPage('dash', this)">Live Dash</button>
            <button class="nav-btn" onclick="switchPage('clusters', this)">Clusters</button>
            <button class="nav-btn" onclick="switchPage('sprockets', this)">Sprockets</button>
            <button class="nav-btn" onclick="switchPage('control', this)">Command Panel</button>
            <button class="nav-btn" onclick="switchPage('race', this)">Race Lobby</button>
            <button class="nav-btn" onclick="switchPage('perf', this)">Drag Times</button>
            <button class="nav-btn" onclick="switchPage('records', this)">Records</button>
            <button class="nav-btn" onclick="switchPage('service', this)">Maintenance</button>
        </div>
    </div>

    <script>
        const canvas = document.getElementById('lfaGauge');
        const ctx = canvas.getContext('2d');
        
        let liveData = { mph: 0, rpm: 0, gear: 'N', odo: 0.0, shift: 14500, flash: 50, stage: '', temp: 72, activeSim: false };
        let renderMph = 0, renderRpm = 0;
        let currentLayout = 'cbr600rr-detail';
        let liveSprocketIndex = '1';
        let sprocketSelectionDirty = false;
        let localRaceRole = 'Solo';
        let localRaceStatus = 'Idle';
        let localRacePartner = 'None';

        function switchPage(pageId, btnEl) {
            document.getElementById('page-dash').style.display = 'none';
            document.getElementById('page-clusters').style.display = 'none';
            document.getElementById('page-sprockets').style.display = 'none';
            document.getElementById('page-control').style.display = 'none';
            document.getElementById('page-perf').style.display = 'none';
            document.getElementById('page-records').style.display = 'none';
            document.getElementById('page-race').style.display = 'none';
            document.getElementById('page-service').style.display = 'none';
            
            document.querySelectorAll('.nav-btn').forEach(b => b.classList.remove('active'));
            
            if(pageId === 'dash') {
                document.getElementById('page-dash').style.display = 'flex';
            } else {
                document.getElementById('page-' + pageId).style.display = 'block';
            }
            btnEl.classList.add('active');
        }

        function toggleSimCore(checkbox) {
            fetch('/toggleSim?active=' + (checkbox.checked ? '1' : '0')).catch(err => console.log('Link drop'));
        }

        function setDataSource(mode) {
            fetch('/setSource?mode=' + mode).catch(err => console.log('Source switch lost'));
            updateSourceButtons(mode);
        }

        function updateSourceButtons(mode) {
            document.getElementById('source-sim').classList.toggle('active', mode === 'sim');
            document.getElementById('source-bike').classList.toggle('active', mode === 'bike');
        }

        function markSprocketSelectionDirty() {
            sprocketSelectionDirty = true;
        }

        function applySprocketSetup() {
            const selector = document.getElementById('sprocket-select');
            const label = selector.options[selector.selectedIndex].text;
            if (!confirm('Change current sprocket setup to ' + label + '?')) {
                selector.value = liveSprocketIndex;
                sprocketSelectionDirty = false;
                return;
            }

            fetch('/setSprocket?idx=' + selector.value)
                .then(res => {
                    if (!res.ok) throw new Error('Sprocket setup rejected');
                    liveSprocketIndex = selector.value;
                    sprocketSelectionDirty = false;
                    document.getElementById('sprocket-status').innerText = 'Error Success';
                    setTimeout(() => document.getElementById('sprocket-status').innerText = '', 2500);
                })
                .catch(err => {
                    document.getElementById('sprocket-status').innerText = 'Error applying setup';
                });
        }

        function setLayout(layoutId) {
            currentLayout = layoutId;
            document.querySelectorAll('.layout-card').forEach(card => card.classList.remove('selected'));
            document.getElementById('lay-' + layoutId).classList.add('selected');
            localStorage.setItem('sfClusterLayout', layoutId);
        }

        function adjustSetting(type, delta) {
            let url = '/adjustConfig?type=' + type + '&delta=' + delta;
            fetch(url).catch(err => console.log('Config sink lost'));
        }

        function resetOilLifeRemote() {
            if(confirm("Confirm full master reset of all three engine oil analytics tracking logs?")) {
                fetch('/resetOil').catch(err => console.log('Wipe failed'));
            }
        }

        function resetTripRemote(tripId) {
            if(confirm("Reset Trip " + tripId + " register?")) {
                fetch('/resetTrip?meter=' + tripId).catch(err => console.log('Trip reset failed'));
            }
        }
        
        window.switchPage = switchPage;
        window.toggleSimCore = toggleSimCore;
        window.setDataSource = setDataSource;
        window.markSprocketSelectionDirty = markSprocketSelectionDirty;
        window.applySprocketSetup = applySprocketSetup;
        window.setLayout = setLayout;
        window.adjustSetting = adjustSetting;
        window.resetOilLifeRemote = resetOilLifeRemote;
        window.resetTripRemote = resetTripRemote;

        const savedClusterLayout = localStorage.getItem('sfClusterLayout');
        if (savedClusterLayout && document.getElementById('lay-' + savedClusterLayout)) setLayout(savedClusterLayout);

        function drawNeedle(cx, cy, angle, len, color, width) {
            ctx.beginPath();
            ctx.moveTo(cx, cy);
            ctx.lineTo(cx + len * Math.cos(angle), cy + len * Math.sin(angle));
            ctx.strokeStyle = color;
            ctx.lineWidth = width;
            ctx.lineCap = 'round';
            ctx.stroke();
            ctx.lineCap = 'butt';
            ctx.beginPath();
            ctx.arc(cx, cy, 8, 0, 2 * Math.PI);
            ctx.fillStyle = '#101014';
            ctx.fill();
            ctx.strokeStyle = '#3a3a3c';
            ctx.lineWidth = 2;
            ctx.stroke();
        }

        function drawRoundGauge(cx, cy, r, minVal, maxVal, value, label, unit, majorStep, redFrom) {
            const start = 0.78 * Math.PI;
            const sweep = 1.44 * Math.PI;
            ctx.beginPath();
            ctx.arc(cx, cy, r + 10, 0, 2 * Math.PI);
            ctx.fillStyle = '#050507';
            ctx.fill();
            ctx.lineWidth = 8;
            ctx.strokeStyle = '#1c1c24';
            ctx.stroke();

            ctx.beginPath();
            ctx.arc(cx, cy, r, 0, 2 * Math.PI);
            ctx.fillStyle = '#f2f2ef';
            ctx.fill();
            ctx.strokeStyle = '#111';
            ctx.lineWidth = 3;
            ctx.stroke();

            ctx.textAlign = 'center';
            ctx.textBaseline = 'middle';
            for (let v = minVal; v <= maxVal; v += majorStep / 2) {
                const isMajor = (Math.abs((v / majorStep) - Math.round(v / majorStep)) < 0.01);
                const angle = start + ((v - minVal) / (maxVal - minVal)) * sweep;
                const outer = r - 8;
                const inner = r - (isMajor ? 20 : 14);
                ctx.beginPath();
                ctx.moveTo(cx + outer * Math.cos(angle), cy + outer * Math.sin(angle));
                ctx.lineTo(cx + inner * Math.cos(angle), cy + inner * Math.sin(angle));
                ctx.strokeStyle = (redFrom && v >= redFrom) ? '#d51f2a' : '#151515';
                ctx.lineWidth = isMajor ? 2 : 1;
                ctx.stroke();
                if (isMajor) {
                    ctx.fillStyle = (redFrom && v >= redFrom) ? '#d51f2a' : '#151515';
                    ctx.font = 'bold 10px sans-serif';
                    ctx.fillText(v, cx + (r - 34) * Math.cos(angle), cy + (r - 34) * Math.sin(angle));
                }
            }

            ctx.fillStyle = '#444';
            ctx.font = 'bold 10px sans-serif';
            ctx.fillText(label, cx, cy - 18);
            ctx.font = '9px sans-serif';
            ctx.fillText(unit, cx, cy + 20);
            const needleAngle = start + ((Math.min(Math.max(value, minVal), maxVal) - minVal) / (maxVal - minVal)) * sweep;
            drawNeedle(cx, cy, needleAngle, r - 28, '#e42b2b', 3);
        }

        function drawIndicator(x, y, label, active, color) {
            ctx.beginPath();
            ctx.arc(x, y, 10, 0, 2 * Math.PI);
            ctx.fillStyle = active ? color : '#16161d';
            ctx.fill();
            ctx.strokeStyle = '#33333d';
            ctx.lineWidth = 2;
            ctx.stroke();
            ctx.fillStyle = active ? '#071007' : '#555';
            ctx.font = 'bold 9px sans-serif';
            ctx.textAlign = 'center';
            ctx.textBaseline = 'middle';
            ctx.fillText(label, x, y);
        }

        function updateGearTracker() {
          // Motorcycle physical order: 1 N 2 3 4 5 6
          const labels = ['1','N','2','3','4','5','6'];
          const container = document.getElementById('gear-tracker');
          container.innerHTML = labels.map(label => {
            const active = liveData.gear === label;
            const stateClass = label === 'N' ? ' neutral' : (label === '6' ? ' red' : ' yellow');
            return `<div class="gear-dot${stateClass}${active ? ' active' : ''}">${label}</div>`;
          }).join('');
        }

        function updateLargeGearDisplay() {}

        function toggleNavCenter(checkbox) {
          const nav = document.querySelector('.nav-bar');
          nav.classList.toggle('nav-center', checkbox.checked);
          localStorage.setItem('sfNavCenter', checkbox.checked ? '1' : '0');
        }

        function loadPortalSettings() {
          const center = localStorage.getItem('sfNavCenter') === '1';
          document.getElementById('setting-nav-center').checked = center;
          document.querySelector('.nav-bar').classList.toggle('nav-center', center);
        }

        // simple client-side gear simulation to validate single-active-light behavior
        function runGearSimulation() {
          const labels = ['1','N','2','3','4','5','6'];
          let i = 0;
          document.getElementById('simToggle').checked = true;
          const tick = setInterval(() => {
            liveData.gear = labels[i % labels.length];
            // set rpm higher for shift gear to exercise flash
            liveData.rpm = (liveData.gear === 'N') ? 500 : 12000 + (i * 500);
            updateGearTracker();
            // verify only one active dot
            const active = document.querySelectorAll('.gear-dot.active');
            if (active.length !== 1) console.warn('Simulation: Unexpected active count', active.length);
            i++;
            if (i > labels.length * 3) {
              clearInterval(tick);
              document.getElementById('simToggle').checked = false;
            }
          }, 550);
        }

        function createRaceSession() {
            fetch('/raceCreate')
                .then(res => res.json())
                .then(d => {
                    document.getElementById('race-origin-token').innerText = d.token || '—';
                    document.getElementById('race-status').innerText = 'Hosting';
                    document.getElementById('race-local-role').innerText = 'Host';
                    localRaceRole = 'Host';
                    localRaceStatus = 'Hosting';
                    localRacePartner = 'None';
                })
                .catch(() => alert('Unable to create race host.'));
        }

        function joinRaceHost() {
            const hostIp = document.getElementById('race-host-ip').value.trim();
            const token = document.getElementById('race-token-input').value.trim();
            if (!hostIp || !token) {
                alert('Enter both host IP and token.');
                return;
            }
            fetch(`http://${hostIp}/raceJoin?token=${encodeURIComponent(token)}`)
                .then(res => {
                    if (!res.ok) throw new Error('Join failed');
                    document.getElementById('race-status').innerText = 'Joined Host';
                    document.getElementById('race-local-role').innerText = 'Guest';
                    document.getElementById('race-partner-ip').innerText = hostIp;
                    localRaceRole = 'Guest';
                    localRaceStatus = 'Joined Host';
                    localRacePartner = hostIp;
                })
                .catch(() => alert('Unable to join host. Check IP / token / network.'));
        }

        function startRaceSession() {
            fetch('/raceStart?token=' + encodeURIComponent(document.getElementById('race-origin-token').innerText))
                .then(res => {
                    if (!res.ok) throw new Error('Start failed');
                    document.getElementById('race-status').innerText = 'Race Active';
                    localRaceStatus = 'Active';
                })
                .catch(() => alert('Unable to start race. Confirm host session first.'));
        }

        function cancelRaceSession() {
            fetch('/raceCancel?token=' + encodeURIComponent(document.getElementById('race-origin-token').innerText))
                .then(res => {
                    if (!res.ok) throw new Error('Cancel failed');
                    document.getElementById('race-status').innerText = 'Idle';
                    document.getElementById('race-local-role').innerText = 'Solo';
                    document.getElementById('race-partner-ip').innerText = 'None';
                    document.getElementById('race-origin-token').innerText = '—';
                    localRaceRole = 'Solo';
                    localRaceStatus = 'Idle';
                    localRacePartner = 'None';
                })
                .catch(() => alert('Unable to cancel race.'));
        }

        function queryHostRaceStatus() {
            const hostIp = document.getElementById('race-host-ip').value.trim();
            if (!hostIp) {
                alert('Enter host IP to check status.');
                return;
            }
            fetch(`http://${hostIp}/raceStatus`)
                .then(res => res.json())
                .then(d => {
                    document.getElementById('race-origin-token').innerText = d.token || '—';
                    document.getElementById('race-partner-ip').innerText = d.partner || 'None';
                    document.getElementById('race-status').innerText = d.status || 'Unknown';
                    document.getElementById('race-local-role').innerText = 'Guest';
                    localRaceRole = 'Guest';
                    localRaceStatus = d.status || 'Unknown';
                    localRacePartner = d.partner || 'None';
                })
                .catch(() => alert('Unable to query host.'));
        }

        function updateRacePage(d) {
            document.getElementById('race-device-ip').innerText = d.hostIp || '--';
            if (d.raceToken) {
                document.getElementById('race-origin-token').innerText = d.raceToken;
            }
            if (localRaceRole === 'Guest') {
                document.getElementById('race-status').innerText = localRaceStatus;
                document.getElementById('race-local-role').innerText = localRaceRole;
                document.getElementById('race-partner-ip').innerText = localRacePartner;
            } else {
                document.getElementById('race-status').innerText = d.raceStatus || 'Idle';
                document.getElementById('race-local-role').innerText = (d.raceStatus === 'hosting' || d.raceStatus === 'paired' || d.raceStatus === 'active') ? 'Host' : 'Solo';
                document.getElementById('race-partner-ip').innerText = d.racePartnerIp || 'None';
            }
        }

        // --- DETAILED DASH: 2005 CBR600RR inspired ---
        function renderCBR600RRDetailed() {
            ctx.fillStyle = '#08080b';
            ctx.fillRect(0, 0, 340, 340);
            ctx.fillStyle = '#14141a';
            ctx.fillRect(34, 50, 272, 210);
            ctx.fillStyle = '#0a0a0d';
            ctx.fillRect(56, 252, 228, 22);

            drawIndicator(138, 34, 'N', liveData.gear === 'N', '#2eff64');
            drawIndicator(170, 34, 'FI', false, '#ff3333');
            drawIndicator(202, 34, 'OIL', false, '#ffcc00');

            drawRoundGauge(102, 154, 70, 0, 180, renderMph, 'SPEED', 'mph', 20, 150);
            drawRoundGauge(238, 154, 70, 0, 15, renderRpm / 1000, 'TACH', 'x1000', 1, 13);

            ctx.fillStyle = '#1c211f';
            ctx.fillRect(65, 214, 76, 26);
            ctx.fillRect(200, 214, 76, 26);
            ctx.fillStyle = '#78d6bd';
            ctx.font = 'bold 14px monospace';
            ctx.textAlign = 'center';
            ctx.fillText(liveData.odo.toFixed(1), 103, 231);
            ctx.fillText(Math.round(renderRpm), 238, 231);

            ctx.fillStyle = '#f5f5f5';
            ctx.font = 'italic bold 17px sans-serif';
            ctx.fillText('HONDO CBR600RR', 170, 294);
            ctx.fillStyle = '#8e8e93';
            ctx.font = 'bold 10px sans-serif';
            ctx.fillText('DETAILED REPLICA - PARODY BADGE', 170, 310);
        }

        // --- DETAILED DASH: 2004-2005 Ducati 800SS inspired ---
        function renderDucati800Detailed() {
            ctx.fillStyle = '#07070a';
            ctx.fillRect(0, 0, 340, 340);
            ctx.fillStyle = '#17171d';
            ctx.beginPath();
            ctx.arc(113, 160, 88, 0, 2 * Math.PI);
            ctx.arc(227, 160, 88, 0, 2 * Math.PI);
            ctx.fill();
            ctx.fillStyle = '#101015';
            ctx.fillRect(146, 67, 48, 190);

            drawIndicator(150, 70, 'N', liveData.gear === 'N', '#32ff63');
            drawIndicator(190, 70, '!', liveData.activeSim && liveData.rpm >= liveData.shift, '#ff3030');
            drawIndicator(170, 100, 'HI', false, '#5fb3ff');

            drawRoundGauge(112, 164, 72, 0, 160, renderMph, 'VELOCITA', 'mph', 20, 130);
            drawRoundGauge(228, 164, 72, 0, 11, renderRpm / 1000, 'GIRI', 'x1000', 1, 9);

            ctx.fillStyle = '#171b18';
            ctx.fillRect(77, 222, 70, 18);
            ctx.fillRect(193, 222, 70, 18);
            ctx.fillStyle = '#d0b46b';
            ctx.font = 'bold 12px monospace';
            ctx.textAlign = 'center';
            ctx.fillText(Math.round(renderMph) + ' MPH', 112, 235);
            ctx.fillText(liveData.gear, 228, 235);

            ctx.fillStyle = '#e7e7e7';
            ctx.font = 'italic bold 18px serif';
            ctx.fillText('DUCKATI 800SS', 170, 288);
            ctx.fillStyle = '#9a9aa3';
            ctx.font = 'bold 10px sans-serif';
            ctx.fillText('TWIN POD ANALOG PARODY', 170, 305);
        }

        // --- DETAILED DASH: 1999 GSX-R600 inspired ---
        function renderGSXR600Detailed() {
            ctx.fillStyle = '#09090c';
            ctx.fillRect(0, 0, 340, 340);
            ctx.fillStyle = '#15151b';
            ctx.beginPath();
            ctx.moveTo(35, 112);
            ctx.quadraticCurveTo(170, 28, 305, 112);
            ctx.lineTo(286, 252);
            ctx.lineTo(54, 252);
            ctx.closePath();
            ctx.fill();
            ctx.strokeStyle = '#2d2d36';
            ctx.lineWidth = 3;
            ctx.stroke();

            const cx = 170, cy = 176, r = 125;
            const start = 1.08 * Math.PI, sweep = 0.86 * Math.PI;
            for (let i = 3; i <= 15; i++) {
                const a = start + ((i - 3) / 12) * sweep;
                ctx.beginPath();
                ctx.arc(cx, cy, r, a, a + 0.035 * Math.PI);
                ctx.lineWidth = 12;
                ctx.strokeStyle = i >= 13 ? '#d51f2a' : (i >= 10 ? '#d7aa34' : '#d8d8d8');
                ctx.stroke();
                ctx.fillStyle = i >= 14 ? '#d51f2a' : '#e8edf2';
                ctx.font = 'bold 14px sans-serif';
                ctx.textAlign = 'center';
                ctx.textBaseline = 'middle';
                ctx.fillText(i, cx + (r - 28) * Math.cos(a), cy + (r - 28) * Math.sin(a));
            }

            const tachAngle = start + ((Math.min(renderRpm / 1000, 15) - 3) / 12) * sweep;
            drawNeedle(cx, cy, tachAngle, 98, '#ff4a31', 4);

            ctx.fillStyle = '#c9d6d2';
            ctx.fillRect(84, 181, 172, 56);
            ctx.strokeStyle = '#404846';
            ctx.strokeRect(84, 181, 172, 56);
            ctx.fillStyle = '#111';
            ctx.font = 'bold 42px monospace';
            ctx.textAlign = 'center';
            ctx.fillText(Math.round(renderMph), 158, 219);
            ctx.font = 'bold 10px sans-serif';
            ctx.fillText('mph', 220, 209);
            ctx.fillText('GEAR ' + liveData.gear, 220, 226);

            drawIndicator(50, 144, 'N', liveData.gear === 'N', '#2eff64');
            drawIndicator(50, 176, 'HI', false, '#5fb3ff');
            drawIndicator(290, 144, 'OIL', false, '#ffb000');
            drawIndicator(290, 176, '!', liveData.activeSim && liveData.rpm >= liveData.shift, '#ff3030');

            ctx.fillStyle = '#f5f5f5';
            ctx.font = 'italic bold 17px sans-serif';
            ctx.fillText('SOOZUKI GSX-R600', 170, 286);
            ctx.fillStyle = '#8e8e93';
            ctx.font = 'bold 10px sans-serif';
            ctx.fillText('ARC TACH / LCD PARODY', 170, 303);
        }

        // --- NEW DASH: 2025 Suzuki GSX-R1000 ---
        function renderGSXR2025() {
            ctx.fillStyle = '#0a0b10';
            ctx.fillRect(0, 30, 340, 280);

            // Sweeping LCD segmented tachometer
            let cx = 170, cy = 180, r = 135;
            let startAngle = 0.8 * Math.PI;
            let endAngle = 2.2 * Math.PI;
            let totalSegments = 45;
            let segmentSweep = (endAngle - startAngle) / totalSegments;

            // Render ghosted background blocks
            ctx.lineWidth = 14;
            for(let i=0; i<totalSegments; i++) {
                ctx.beginPath();
                let a1 = startAngle + i * segmentSweep;
                let a2 = a1 + segmentSweep * 0.75; 
                ctx.arc(cx, cy, r, a1, a2);
                ctx.strokeStyle = '#1c1c24';
                ctx.stroke();
            }

            // Render active foreground blocks
            let activeSegments = Math.floor((Math.min(renderRpm, 15000) / 15000) * totalSegments);
            for(let i=0; i<activeSegments; i++) {
                ctx.beginPath();
                let a1 = startAngle + i * segmentSweep;
                let a2 = a1 + segmentSweep * 0.75;
                ctx.arc(cx, cy, r, a1, a2);
                
                let segColor = (i > totalSegments * 0.8) ? '#ff3b30' : '#ffffff';
                if (liveData.rpm >= liveData.shift) segColor = '#ff3b30'; 
                ctx.strokeStyle = segColor;
                ctx.stroke();
            }

            // RPM Numerals
            ctx.fillStyle = '#8e8e93'; ctx.font = 'bold 10px sans-serif'; ctx.textAlign = 'center';
            for(let i=0; i<=15; i+=3) {
                let angle = startAngle + (i/15) * (endAngle - startAngle);
                ctx.fillText(i, cx + (r - 26) * Math.cos(angle), cy + (r - 26) * Math.sin(angle));
            }

            // Central Speedometer
            ctx.fillStyle = '#ffffff'; ctx.font = 'bold 84px monospace'; ctx.textAlign = 'center';
            ctx.fillText(Math.round(renderMph), cx, cy + 30);
            ctx.fillStyle = '#8e8e93'; ctx.font = 'bold 14px sans-serif';
            ctx.fillText('MPH', cx, cy + 55);

            // Left side ghosted indicators (Gear)
            ctx.fillStyle = (liveData.gear === 'N') ? '#34c759' : '#ffffff';
            ctx.font = 'bold 48px sans-serif'; ctx.textAlign = 'center';
            ctx.fillText(liveData.gear, cx - 110, cy + 30);
            ctx.fillStyle = '#8e8e93'; ctx.font = '10px sans-serif';
            ctx.fillText('GEAR', cx - 110, cy - 15);

            // Right side ghosted indicators (Modes)
            ctx.fillStyle = '#007aff'; ctx.font = 'bold 16px sans-serif'; ctx.textAlign = 'center';
            ctx.fillText('A', cx + 110, cy);
            ctx.fillStyle = '#8e8e93'; ctx.font = '10px sans-serif';
            ctx.fillText('S-DMS', cx + 110, cy - 15);
            
            ctx.fillStyle = '#ffcc00'; ctx.font = 'bold 16px sans-serif';
            ctx.fillText('3', cx + 110, cy + 40);
            ctx.fillStyle = '#8e8e93'; ctx.font = '10px sans-serif';
            ctx.fillText('TC', cx + 110, cy + 25);
            
            // GSX-R Logo 
            ctx.fillStyle = '#ff3b30'; ctx.font = 'italic bold 16px sans-serif'; ctx.textAlign = 'center';
            ctx.fillText('R', cx - 20, cy + 95);
            ctx.fillStyle = '#ffffff'; ctx.font = 'italic bold 16px sans-serif';
            ctx.fillText('GSX', cx + 12, cy + 95);
        }

        // --- NEW DASH: 2026 Kawasaki ZX-10R ---
        function renderZX10R2026() {
            ctx.fillStyle = '#050505';
            ctx.fillRect(0, 0, 340, 340);

            // Kawasaki RIDEOLOGY TFT Branding
            ctx.fillStyle = '#34c759'; 
            ctx.font = 'italic bold 14px sans-serif'; ctx.textAlign = 'left';
            ctx.fillText('RIDEOLOGY', 15, 25);
            
            ctx.fillStyle = '#8e8e93'; ctx.font = 'bold 12px sans-serif'; ctx.textAlign = 'right';
            ctx.fillText('KTRC  2', 325, 25);

            // Smooth TFT Circular Tachometer Background
            let cx = 170, cy = 165, r = 115;
            ctx.beginPath();
            ctx.arc(cx, cy, r, 0.7 * Math.PI, 2.3 * Math.PI);
            ctx.lineWidth = 16;
            ctx.strokeStyle = '#1a1a1a';
            ctx.stroke();

            // Dynamic Sweeping Foreground
            let rpmRatio = Math.min(renderRpm, 15000) / 15000;
            let activeEnd = 0.7 * Math.PI + rpmRatio * (1.6 * Math.PI);
            ctx.beginPath();
            ctx.arc(cx, cy, r, 0.7 * Math.PI, activeEnd);
            
            // Color gradient scaling for RPM
            let tColor = '#34c759'; 
            if (renderRpm > 11000) tColor = '#ffcc00';
            if (renderRpm > 13500) tColor = '#ff3b30';
            if (liveData.rpm >= liveData.shift) tColor = '#ff3b30';
            ctx.strokeStyle = tColor;
            ctx.stroke();

            // Internal RPM markers
            ctx.fillStyle = '#ffffff'; ctx.font = 'bold 12px sans-serif'; ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
            for (let i = 0; i <= 15; i += 3) {
                let angle = 0.7 * Math.PI + (i / 15) * (1.6 * Math.PI);
                ctx.fillText(i, cx + (r - 28) * Math.cos(angle), cy + (r - 28) * Math.sin(angle));
            }

            // High Fidelity Speed & Data
            ctx.fillStyle = '#ffffff'; ctx.font = 'bold 76px monospace';
            ctx.fillText(Math.round(renderMph), cx, cy + 15);
            ctx.fillStyle = '#8e8e93'; ctx.font = 'bold 13px sans-serif';
            ctx.fillText('MPH', cx, cy + 45); 

            // Left Mode, Right Gear layout mimicking TFT
            ctx.fillStyle = (liveData.gear === 'N') ? '#34c759' : '#ffffff';
            ctx.font = 'bold 36px sans-serif';
            ctx.fillText(liveData.gear, cx + 80, cy + 85);
            ctx.fillStyle = '#8e8e93'; ctx.font = '10px sans-serif';
            ctx.fillText('GEAR', cx + 80, cy + 55);
            
            ctx.fillStyle = '#007aff'; ctx.font = 'bold 16px sans-serif';
            ctx.fillText('ROAD', cx - 80, cy + 85);
            ctx.fillStyle = '#8e8e93'; ctx.font = '10px sans-serif';
            ctx.fillText('MODE', cx - 80, cy + 65);
        }

        // --- LEGACY DASHBOARDS ---
        function renderLFAPod() {
            ctx.beginPath();
            const cx = 170, cy = 170, r = 145;
            ctx.arc(cx, cy, r, 0.75 * Math.PI, 2.25 * Math.PI);
            ctx.strokeStyle = (liveData.rpm >= liveData.shift) ? '#ff3b30' : '#22222b';
            ctx.lineWidth = 6;
            ctx.stroke();

            let startAngle = 0.75 * Math.PI;
            let endAngle = startAngle + (Math.min(renderRpm, 16000) / 16000) * (1.5 * Math.PI);
            ctx.beginPath();
            ctx.arc(cx, cy, r - 3, startAngle, endAngle);
            ctx.strokeStyle = (liveData.rpm >= liveData.shift) ? '#ff3b30' : '#007aff';
            ctx.lineWidth = 12;
            ctx.stroke();

            ctx.fillStyle = '#7a7a85'; ctx.font = 'bold 12px sans-serif'; ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
            for(let i = 0; i <= 16; i += 2) {
                let angle = startAngle + (i / 16) * (1.5 * Math.PI);
                ctx.fillText(i, cx + (r - 24) * Math.cos(angle), cy + (r - 24) * Math.sin(angle));
            }
            ctx.fillStyle = '#ffffff'; ctx.font = 'bold 74px sans-serif'; ctx.fillText(Math.round(renderMph), cx, cy - 10);
            ctx.fillStyle = '#8e8e93'; ctx.font = '800 13px sans-serif'; ctx.fillText('MPH', cx, cy + 34);
            ctx.fillStyle = (liveData.gear === 'N') ? '#34c759' : ((liveData.rpm >= liveData.shift) ? '#ff3b30' : '#007aff');
            ctx.font = 'bold 36px sans-serif'; ctx.fillText(liveData.gear, cx, cy + 78);
        }

        function renderS2000Dash() {
            ctx.fillStyle = '#1c1c24'; ctx.fillRect(20, 60, 300, 30);
            let segments = Math.floor((Math.min(renderRpm, 16000) / 16000) * 300);
            
            let barColor = '#ffcc00';
            if (renderRpm >= 11000) barColor = '#ff9500';
            if (renderRpm >= liveData.shift) barColor = '#ff3b30';
            ctx.fillStyle = barColor; ctx.fillRect(20, 60, segments, 30);

            ctx.fillStyle = '#555562'; ctx.font = 'bold 11px monospace'; ctx.textAlign = 'center'; ctx.textBaseline = 'alphabetic';
            for(let i = 0; i <= 16; i += 2) {
                ctx.fillText(i, 20 + (i/16)*300, 52);
            }
            ctx.fillText('RPM x1000', 170, 105);

            ctx.fillStyle = '#ffffff'; ctx.font = 'bold 84px monospace'; ctx.textAlign = 'right';
            ctx.fillText(Math.round(renderMph), 210, 205);
            ctx.fillStyle = '#8e8e93'; ctx.font = 'bold 18px sans-serif'; ctx.textAlign = 'left';
            ctx.fillText('MPH', 220, 165);

            ctx.fillStyle = '#14141c'; ctx.fillRect(220, 175, 45, 45);
            ctx.strokeStyle = '#242430'; ctx.strokeRect(220, 175, 45, 45);
            ctx.fillStyle = (liveData.gear==='N') ? '#34c759' : '#ff9500'; ctx.font = 'bold 28px sans-serif'; ctx.textAlign = 'center';
            ctx.fillText(liveData.gear, 242, 208);
        }

        function renderDucatiDash() {
            let startA = 0.9 * Math.PI, endA = 2.1 * Math.PI;
            ctx.beginPath(); ctx.arc(170, 180, 120, startA, endA);
            ctx.strokeStyle = '#1c1c24'; ctx.lineWidth = 8; ctx.stroke();

            let currEnd = startA + (Math.min(renderRpm, 16000)/16000)*(1.2 * Math.PI);
            ctx.beginPath(); ctx.arc(170, 180, 120, startA, currEnd);
            ctx.strokeStyle = (renderRpm >= liveData.shift) ? '#ff3b30' : '#ffffff'; ctx.lineWidth = 8; ctx.stroke();

            ctx.fillStyle = '#636366'; ctx.font = '10px sans-serif'; ctx.textAlign = 'center'; ctx.textBaseline = 'alphabetic';
            for(let i=0; i<=16; i+=4) {
                let a = startA + (i/16)*(1.2*Math.PI);
                ctx.fillText(i, 170 + 134*Math.cos(a), 180 + 134*Math.sin(a));
            }

            ctx.fillStyle = (liveData.gear==='N') ? '#34c759' : '#ff3b30'; ctx.font = 'bold 72px sans-serif'; ctx.textAlign = 'center';
            ctx.fillText(liveData.gear, 110, 195);

            ctx.fillStyle = '#ffffff'; ctx.font = 'bold 58px sans-serif'; ctx.textAlign = 'center';
            ctx.fillText(Math.round(renderMph), 225, 180);
            ctx.fillStyle = '#8e8e93'; ctx.font = 'bold 12px sans-serif';
            ctx.fillText('MPH', 225, 205);
        }

        function renderCBR05Dash() {
            ctx.fillStyle = '#101413'; ctx.fillRect(15, 80, 150, 160);
            ctx.strokeStyle = '#2d3835'; ctx.lineWidth = 3; ctx.strokeRect(15, 80, 150, 160);
            
            ctx.fillStyle = '#7be0cb'; ctx.font = 'bold 54px monospace'; ctx.textAlign = 'right'; ctx.textBaseline = 'alphabetic';
            ctx.fillText(Math.round(renderMph), 125, 145);
            ctx.font = 'bold 12px sans-serif'; ctx.textAlign = 'left'; ctx.fillText('MPH', 130, 140);
            
            ctx.fillStyle = '#59a696'; ctx.font = '12px monospace';
            ctx.fillText('TEMP: ' + liveData.temp + ' F', 25, 185);
            ctx.fillText('ODO: ' + liveData.odo.toFixed(1) + ' mi', 25, 205);
            ctx.fillStyle = (liveData.gear === 'N') ? '#34c759' : '#59a696';
            ctx.fillText('GEAR: ' + liveData.gear, 25, 225);

            const tx = 245, ty = 160, tr = 75;
            ctx.beginPath(); ctx.arc(tx, ty, tr, 0.8 * Math.PI, 2.2 * Math.PI);
            ctx.strokeStyle = '#22222b'; ctx.lineWidth = 5; ctx.stroke();
            
            ctx.fillStyle = '#8e8e93'; ctx.font = '10px sans-serif'; ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
            for (let i = 0; i <= 15; i += 3) {
                let angle = 0.8 * Math.PI + (i / 15) * (1.4 * Math.PI);
                if (i >= 15) ctx.fillStyle = '#ff3b30'; 
                ctx.fillText(i, tx + (tr - 14) * Math.cos(angle), ty + (tr - 14) * Math.sin(angle));
            }
            
            let needleAngle = 0.8 * Math.PI + (Math.min(renderRpm, 15000) / 15000) * (1.4 * Math.PI);
            ctx.beginPath(); ctx.moveTo(tx, ty);
            ctx.lineTo(tx + (tr - 5) * Math.cos(needleAngle), ty + (tr - 5) * Math.sin(needleAngle));
            ctx.strokeStyle = '#ff9500'; ctx.lineWidth = 3; ctx.stroke();
            
            ctx.beginPath(); ctx.arc(tx, ty, 6, 0, 2*Math.PI);
            ctx.fillStyle = '#1c1c24'; ctx.fill();
        }

        function loopRender() {
            window.requestAnimationFrame(loopRender);
            if (document.getElementById('page-dash').style.display == 'none') return;

            renderMph += (liveData.mph - renderMph) * 0.35;
            renderRpm += (liveData.rpm - renderRpm) * 0.35;

            ctx.clearRect(0, 0, 340, 340);
            const isOverrev = (liveData.activeSim && liveData.rpm >= liveData.shift);
            const masterPanel = document.getElementById('master-panel');
            
            if (isOverrev) masterPanel.classList.add('shift-flash-active');
            else masterPanel.classList.remove('shift-flash-active');

            if(currentLayout === 'cbr600rr-detail') renderCBR600RRDetailed();
            else if(currentLayout === 'ducati800-detail') renderDucati800Detailed();
            else if(currentLayout === 'gsxr600-detail') renderGSXR600Detailed();
            else if(currentLayout === 'gsxr25') renderGSXR2025();
            else if(currentLayout === 'zx10r26') renderZX10R2026();
            else if(currentLayout === 'lfa') renderLFAPod();
            else if(currentLayout === 's2000') renderS2000Dash();
            else if(currentLayout === 'ducati') renderDucatiDash();
            else if(currentLayout === 'cbr05') renderCBR05Dash();
        }

        setInterval(function() {
            fetch('/data')
                .then(res => res.json())
                .then(d => {
                    liveData.mph = d.mph; liveData.rpm = d.rpm; liveData.gear = d.gear;
                    liveData.shift = d.shift; liveData.flash = d.flash; liveData.stage = d.stage;
                    liveData.odo = d.odo; liveData.temp = d.temp; liveData.activeSim = d.activeSim;

                    updateSourceButtons(d.source);
                    document.getElementById('simToggle').checked = d.activeSim;
                    document.getElementById('web-shift').innerText = d.shift;
                    document.getElementById('web-flash').innerText = d.flash;
                    document.getElementById('pin-speed-in').innerText = 'GPIO ' + d.pinSpeedIn;
                    document.getElementById('pin-rpm-in').innerText = 'GPIO ' + d.pinRpmIn;
                    document.getElementById('pin-neutral-in').innerText = 'GPIO ' + d.pinNeutralIn;
                    document.getElementById('pin-kickstand-in').innerText = 'GPIO ' + d.pinKickstandIn;
                    document.getElementById('pin-speed-out').innerText = 'GPIO ' + d.pinSpeedOut;
                    document.getElementById('input-polarity').innerText = d.inputActiveLow ? 'Active Low' : 'Active High';
                    document.getElementById('speed-ppr').innerText = d.speedPpr;
                    document.getElementById('rpm-ppr').innerText = d.rpmPpr;
                    
                    if(d.isDemo) {
                        document.getElementById('sprocket-banner').innerHTML = `SPROCKET: <span style="color:#ffcc00; font-weight:800;">[DEMO LOOP] ${d.frontT}T/${d.rearT}T</span>`;
                    } else {
                        document.getElementById('sprocket-banner').innerHTML = `SPROCKET: <span>${d.frontT}T / ${d.rearT}T</span>`;
                    }
                    
                    document.getElementById('stock-setup').innerText = d.stockFront + 'T / ' + d.stockRear + 'T';
                    document.getElementById('current-setup').innerText = d.currentFront + 'T / ' + d.currentRear + 'T';
                    liveSprocketIndex = String(d.setupIndex);
                    if (!sprocketSelectionDirty) document.getElementById('sprocket-select').value = liveSprocketIndex;
                    document.getElementById('stock-final').innerText = d.stockFinal.toFixed(4);
                    document.getElementById('current-final').innerText = d.currentFinal.toFixed(4);
                    document.getElementById('correction-factor').innerText = d.correctionFactor.toFixed(4);
                    document.getElementById('corrected-100').innerText = (100 * d.correctionFactor).toFixed(1) + ' MPH';
                    document.getElementById('ch-speed').innerText = d.speedStage;
                    document.getElementById('ch-rpm').innerText = d.stage;
                    document.getElementById('ch-neutral').innerText = d.neutralStage;
                    document.getElementById('ch-kickstand').innerText = d.kickstandStage;
                    
                    document.getElementById('p-60').innerText = d.t60.toFixed(2) + ' s';
                    document.getElementById('p-100').innerText = d.t100.toFixed(2) + ' s';
                    document.getElementById('p-quarter').innerText = d.tQ.toFixed(2) + ' s';
                    document.getElementById('p-trap').innerText = d.tS + ' MPH';

                    document.getElementById('r-speed').innerText = d.maxM + ' MPH';
                    document.getElementById('r-rpm').innerText = d.maxR.toLocaleString() + ' RPM';
                    document.getElementById('r-60').innerText = d.b60.toFixed(2) + ' s';
                    document.getElementById('r-quarter').innerText = d.bQ.toFixed(2) + ' s';

                    document.getElementById('s-hours').innerText = d.hrs.toFixed(4) + ' hrs';
                    document.getElementById('s-oil').innerText = d.oil + '%';
                    document.getElementById('s-oil-miles').innerText = d.oilM.toFixed(2) + ' mi';
                    document.getElementById('s-oil-hours').innerText = d.oilH.toFixed(4) + ' hrs';
                    document.getElementById('s-tripa').innerText = d.trA.toFixed(2) + ' mi';
                    document.getElementById('s-tripb').innerText = d.trB.toFixed(2) + ' mi';
                    document.getElementById('version-badge').innerText = 'FW ' + d.version;
                    document.getElementById('current-version').innerText = d.version;
                    if (d.versionHistory && Array.isArray(d.versionHistory) && d.versionHistory.length > 0) {
                        document.getElementById('version-notes').innerText = d.versionHistory[0].notes || '';
                    }
                    updateGearTracker();
                    updateLargeGearDisplay();
                    updateRacePage(d);
                })
                .catch(err => console.log('Thread link drop'));
        }, 150);

        loadPortalSettings();
        window.requestAnimationFrame(loopRender);
    </script>
</body>
</html>
)rawliteral";

float getGearingRatio(int gear) {
  float gearRatios[] = {0.0, 2.666, 1.937, 1.611, 1.409, 1.261, 1.166};
  return gearRatios[gear];
}

void IRAM_ATTR onSpeedPulse() {
  unsigned long now = micros();
  if (lastSpeedPulseMicros != 0) {
    unsigned long interval = now - lastSpeedPulseMicros;
    if (interval >= 100 && interval < 1000000) {
      lastSpeedIntervalMicros = interval;
    }
  }
  lastSpeedPulseMicros = now;
}

void IRAM_ATTR onRPMPulse() {
  unsigned long now = micros();
  if (lastRpmPulseMicros != 0) {
    unsigned long interval = now - lastRpmPulseMicros;
    if (interval >= 100 && interval < 1000000) {
      lastRpmIntervalMicros = interval;
    }
  }
  lastRpmPulseMicros = now;
}

float calculateEffectiveCorrectionFactor() {
  float stockFinalDrive = (float)STOCK_REAR_TEETH / STOCK_FRONT_TEETH;
  float currentFinalDrive = (float)currentRearTeeth / currentFrontTeeth;
  return (stockFinalDrive * STOCK_TIRE_CIRCUMFERENCE_MILES) / (currentFinalDrive * currentTireCircumferenceMiles);
}

int inferGearFromInputs(int rpm, int mph, float finalDrive) {
  if (rpm < 800 || mph <= 1.0) return currentGearNum;
  float wheelRPM = mph / (currentTireCircumferenceMiles * 60.0);
  if (wheelRPM <= 1.0) return currentGearNum;

  float targetRatio = (rpm / wheelRPM) / (PRIMARY_DRIVE_RATIO * finalDrive);
  int bestGear = currentGearNum;
  float bestDiff = 999.0;
  for (int g = 1; g <= 6; g++) {
    float diff = abs(targetRatio - getGearingRatio(g));
    if (diff < bestDiff) {
      bestDiff = diff;
      bestGear = g;
    }
  }
  if (bestDiff > 0.20) return currentGearNum;
  return bestGear;
}

void setSpeedOutputFrequency(float frequencyHz) {
  if (frequencyHz > 0.5) {
    ledcWriteTone(SPEED_OUT_CHANNEL, (int)frequencyHz);
  } else {
    ledcWriteTone(SPEED_OUT_CHANNEL, 0);
    digitalWrite(PIN_SPEED_OUT, LOW);
  }
}

void processBikeTelemetry() {
  bool neutralActive = (digitalRead(PIN_NEUTRAL_IN) == LOW);
  bool kickstandActive = (digitalRead(PIN_KICKSTAND_IN) == LOW);

  optoNeutralStatus = neutralActive ? "NEUTRAL" : "GEARED";
  optoKickstandStatus = kickstandActive ? "DOWN" : "UP";

  unsigned long speedInterval;
  unsigned long rpmInterval;
  noInterrupts();
  speedInterval = lastSpeedIntervalMicros;
  rpmInterval = lastRpmIntervalMicros;
  interrupts();

  if (speedInterval > 0 && micros() - lastSpeedPulseMicros < 500000) {
    float frontRPM = 60000000.0f / (speedInterval * SPEED_PULSES_PER_REV);
    float wheelRPM = frontRPM / ((float)currentRearTeeth / currentFrontTeeth);
    currentMPH = (int)round(wheelRPM * currentTireCircumferenceMiles * 60.0f);
    optoSPDStatus = "SPD_ACTIVE";
  } else {
    currentMPH = 0;
    optoSPDStatus = "SPD_MISSING";
  }

  if (rpmInterval > 0 && micros() - lastRpmPulseMicros < 500000) {
    currentRPM = (int)round(60000000.0f / (rpmInterval * RPM_PULSES_PER_REV));
    optoRPMStatus = "RPM_ACTIVE";
  } else {
    currentRPM = 0;
    optoRPMStatus = "RPM_MISSING";
  }

  if (neutralActive) {
    currentGearNum = 0;
    currentGear = "N";
  } else {
    int newGear = inferGearFromInputs(currentRPM, currentMPH, (float)currentRearTeeth / currentFrontTeeth);
    if (newGear > 0) currentGearNum = newGear;
    currentGear = String(currentGearNum);
  }

  float inputFrequency = 0.0;
  if (speedInterval > 0) {
    inputFrequency = 1000000.0f / speedInterval;
  }
  float correction = calculateEffectiveCorrectionFactor();
  setSpeedOutputFrequency(inputFrequency * correction);
}

int calculatePhysicsMPH(int rpm, int gear, float finalDrive) {
  if (gear == 0) return 0;
  float wheelRPM = rpm / (PRIMARY_DRIVE_RATIO * getGearingRatio(gear) * finalDrive);
  return (int)round(wheelRPM * STOCK_TIRE_CIRCUMFERENCE_MILES * 60.0);
}

void runAdvancedCBRSimulation() {
  if (telemetrySource == SOURCE_BIKE_INPUTS) {
    processBikeTelemetry();
    return;
  }

  if (telemetrySource != SOURCE_SIMULATOR) {
    currentGearNum = 0; currentGear = "N"; currentMPH = 0; currentRPM = 0;
    optoRPMStatus = "BIKE_ARMED"; optoSPDStatus = "BIKE_ARMED";
    optoNeutralStatus = "BIKE_ARMED"; optoKickstandStatus = "BIKE_ARMED";
    return;
  }

  if (!isSimulationActive) {
    currentGearNum = 0; currentGear = "N"; currentMPH = 0; currentRPM = 0;
    optoRPMStatus = "CORE_HALT"; return;
  }

  if (millis() - lastSimUpdate < 30) return; 
  float dt = (millis() - lastSimUpdate) / 1000.0;
  lastSimUpdate = millis();

  if (currentRPM > 4000) {
    coolantTempF = 184 + (currentRPM / 1800) + random(-1, 2);
    if(coolantTempF > 224) coolantTempF = 224;
  } else {
    coolantTempF = 184 + random(-1, 1);
  }

  if (currentMPH > maxRecordedSpeed) maxRecordedSpeed = currentMPH;
  if (currentRPM > maxRecordedRPM) maxRecordedRPM = currentRPM;

  if (currentMPH > 0) {
    float frameDistance = (currentMPH / 3600.0) * dt;
    totalOdometer += frameDistance;
    tripA += frameDistance;
    tripB += frameDistance;
    milesOnCurrentOil += frameDistance;
    
    float hourlyDelta = (dt / 3600.0);
    engineHours += hourlyDelta;
    hoursOnCurrentOil += hourlyDelta;

    int projectedDepletion = (int)(100.0 - (milesOnCurrentOil / 30.0));
    oilLifePercent = constrain(projectedDepletion, 0, 100);
  }

  if (currentTimerState == READY_TO_LAUNCH && currentMPH > 0) {
    currentTimerState = RACING;
    runStartTime = millis();
    dragDistanceFeet = 0.0; time0To60 = 0.0; time0To100 = 0.0; timeQuarterMile = 0.0;
    speedQuarterMile = 0; achieved60 = false; achieved100 = false;
  }
  if (currentTimerState == RACING) {
    float elapsedSeconds = (millis() - runStartTime) / 1000.0;
    dragDistanceFeet += (currentMPH * 1.46667) * dt;
    if (currentMPH >= 60 && !achieved60) { time0To60 = elapsedSeconds; achieved60 = true; }
    if (currentMPH >= 100 && !achieved100) { time0To100 = elapsedSeconds; achieved100 = true; }
    if (dragDistanceFeet >= 1320.0) {
      timeQuarterMile = elapsedSeconds; speedQuarterMile = currentMPH;
      runFinishTime = millis(); currentTimerState = RUN_COMPLETE;
    }
  }
  if (currentTimerState == RUN_COMPLETE) {
    if (millis() - runFinishTime >= TICKET_DISPLAY_DURATION) {
      if (achieved60 && time0To60 < best0To60) best0To60 = time0To60;
      if (achieved100 && time0To100 < best0To100) best0To100 = time0To100;
      if (timeQuarterMile < bestQuarterMile) { bestQuarterMile = timeQuarterMile; bestSpeedQuarterMile = speedQuarterMile; }
      currentTimerState = READY_TO_LAUNCH;
    }
  }

  SprocketSetup &activeSetup = simulationSetups[currentSetupIndex];
  
  switch (dragSimState) {
    case DRAG_IDLE:
      currentGearNum = 0; currentGear = "N"; currentMPH = 0;
      currentRPM = random(1050, 1100);
      optoRPMStatus = "SIM_IDLE"; optoSPDStatus = "STATIONARY";
      if (simStateTimer == 0) simStateTimer = millis();
      if (millis() - simStateTimer >= 10000) { 
        dragSimState = DRAG_STAGE_ROLL;
        dragDistanceFeet = 0.0;
        simStateTimer = 0;
      }
      break;

    case DRAG_STAGE_ROLL:
      currentGearNum = 1; currentGear = "1";
      currentRPM = 1400;
      currentMPH = calculatePhysicsMPH(currentRPM, 1, activeSetup.finalDriveRatio);
      dragDistanceFeet += (currentMPH * 1.46667) * dt;
      optoRPMStatus = "STAGE_ROLL"; optoSPDStatus = "MOVING";
      if (dragDistanceFeet >= 25.0) { 
        dragSimState = DRAG_BURNOUT;
        simStateTimer = millis();
      }
      break;

    case DRAG_BURNOUT:
      currentGearNum = 1; currentGear = "1";
      currentRPM = 15000 + random(-150, 150); 
      currentMPH = calculatePhysicsMPH(14800, 1, activeSetup.finalDriveRatio); 
      optoRPMStatus = "BURNOUT_WOT"; optoSPDStatus = "WHEEL_SPIN";
      if (millis() - simStateTimer >= 5500) { 
        dragSimState = DRAG_PRE_LAUNCH;
        simStateTimer = millis();
      }
      break;

    case DRAG_PRE_LAUNCH:
      currentGearNum = 0; currentGear = "N"; currentMPH = 0;
      currentRPM = 4000; 
      optoRPMStatus = "LAUNCH_PREP"; optoSPDStatus = "STATIONARY";
      if (millis() - simStateTimer >= 8000) { 
        dragSimState = DRAG_RACE_WOT;
        dragDistanceFeet = 0.0;
        currentGearNum = 1; currentGear = "1";
        runStartTime = millis();
        achieved60 = false; achieved100 = false;
        time0To60 = 0.0; time0To100 = 0.0;
      }
      break;

    case DRAG_RACE_WOT:
      optoRPMStatus = "RACE_TRACK"; optoSPDStatus = "ACCELERATING";
      currentRPM += (240 - (currentGearNum * 22)); 
      currentMPH = calculatePhysicsMPH(currentRPM, currentGearNum, activeSetup.finalDriveRatio);
      dragDistanceFeet += (currentMPH * 1.46667) * dt;

      {
        float raceElapsed = (millis() - runStartTime) / 1000.0;
        if (currentMPH >= 60 && !achieved60) { time0To60 = raceElapsed; achieved60 = true; }
        if (currentMPH >= 100 && !achieved100) { time0To100 = raceElapsed; achieved100 = true; }
      }

      if (currentRPM >= 15000) {
        if (currentGearNum < 6) {
          currentGearNum++;
          currentGear = String(currentGearNum);
          currentRPM = 11800; 
        } else {
          currentRPM = 15000;
        }
      }

      if (dragDistanceFeet >= 1320.0) { 
        activeSetup.quarterMileTime = (millis() - runStartTime) / 1000.0;
        activeSetup.quarterMileSpeed = currentMPH;
        activeSetup.run0To60 = time0To60;
        activeSetup.run0To100 = time0To100;
        activeSetup.recorded = true;

        if (activeSetup.run0To60 < best0To60) best0To60 = activeSetup.run0To60;
        if (activeSetup.run0To100 < best0To100) best0To100 = activeSetup.run0To100;
        if (activeSetup.quarterMileTime < bestQuarterMile) {
          bestQuarterMile = activeSetup.quarterMileTime;
          bestSpeedQuarterMile = activeSetup.quarterMileSpeed;
        }

        dragSimState = DRAG_COMPLETE;
        simStateTimer = millis();
      }
      break;

    case DRAG_COMPLETE:
      currentGearNum = 0; currentGear = "N"; currentMPH = 0;
      currentRPM = 1080;
      optoRPMStatus = "RUN_DONE"; optoSPDStatus = "COOLDOWN";
      if (millis() - simStateTimer >= 5000) { 
        if (isDemoModeActive) {
          currentSetupIndex = (currentSetupIndex + 1) % 4;
        }
        dragSimState = DRAG_IDLE;
        simStateTimer = 0;
      }
      break;
  }
}

void handleRoot() {
  server.send_P(200, PSTR("text/html"), index_html);
}

const char* raceStateToString(RaceState state) {
  switch (state) {
    case RACE_HOSTING: return "hosting";
    case RACE_PAIRED:  return "paired";
    case RACE_ACTIVE:  return "active";
    default:           return "idle";
  }
}

const char* telemetrySourceToString(TelemetrySource source) {
  return source == SOURCE_BIKE_INPUTS ? "bike" : "sim";
}

inline const char* jsonBool(bool value) {
  return value ? "true" : "false";
}

void appendJsonField(String &json, const char *key, const String &value, bool quote = true) {
  json += "\"";
  json += key;
  json += "\":";
  if (quote) {
    json += "\"";
    json += value;
    json += "\"";
  } else {
    json += value;
  }
  json += ",";
}

void appendJsonField(String &json, const char *key, int value) {
  appendJsonField(json, key, String(value), false);
}

void appendJsonField(String &json, const char *key, float value, int precision = 2) {
  json += "\"";
  json += key;
  json += "\":";
  json += String(value, precision);
  json += ",";
}

void appendJsonField(String &json, const char *key, bool value) {
  json += "\"";
  json += key;
  json += "\":";
  json += jsonBool(value);
  json += ",";
}

void handleData() {
  float stockFinalDrive = (float)STOCK_REAR_TEETH / (float)STOCK_FRONT_TEETH;
  float currentFinalDrive = (float)currentRearTeeth / (float)currentFrontTeeth;
  float correctionFactor = stockFinalDrive / currentFinalDrive;

  String json = "{";
  appendJsonField(json, "mph", currentMPH);
  appendJsonField(json, "rpm", currentRPM);
  appendJsonField(json, "gear", currentGear);
  appendJsonField(json, "odo", totalOdometer, 1);
  appendJsonField(json, "shift", userShiftPoint);
  appendJsonField(json, "flash", lightFlashDelay);
  appendJsonField(json, "stage", optoRPMStatus);
  appendJsonField(json, "speedStage", optoSPDStatus);
  appendJsonField(json, "neutralStage", optoNeutralStatus);
  appendJsonField(json, "kickstandStage", optoKickstandStatus);
  appendJsonField(json, "temp", coolantTempF);
  appendJsonField(json, "activeSim", isSimulationActive);
  appendJsonField(json, "source", telemetrySourceToString(telemetrySource));
  appendJsonField(json, "pinSpeedIn", PIN_SPEED_IN);
  appendJsonField(json, "pinRpmIn", PIN_RPM_IN);
  appendJsonField(json, "pinNeutralIn", PIN_NEUTRAL_IN);
  appendJsonField(json, "pinKickstandIn", PIN_KICKSTAND_IN);
  appendJsonField(json, "pinSpeedOut", PIN_SPEED_OUT);
  appendJsonField(json, "speedPpr", SPEED_PULSES_PER_REV);
  appendJsonField(json, "rpmPpr", RPM_PULSES_PER_REV);
  appendJsonField(json, "inputActiveLow", INPUT_ACTIVE_LOW);
  appendJsonField(json, "outputActiveLow", OUTPUT_ACTIVE_LOW);
  appendJsonField(json, "isDemo", isDemoModeActive);
  appendJsonField(json, "version", FIRMWARE_VERSION);
  appendJsonField(json, "versionHistory", String(FIRMWARE_CHANGELOG), false);
  appendJsonField(json, "hostIp", WiFi.softAPIP().toString());
  appendJsonField(json, "raceStatus", raceStateToString(raceState));
  appendJsonField(json, "raceToken", raceHostToken);
  appendJsonField(json, "racePartnerIp", racePartnerIp);
  appendJsonField(json, "frontT", simulationSetups[currentSetupIndex].frontTeeth);
  appendJsonField(json, "rearT", simulationSetups[currentSetupIndex].rearTeeth);
  appendJsonField(json, "setupIndex", currentSetupIndex);
  appendJsonField(json, "stockFront", STOCK_FRONT_TEETH);
  appendJsonField(json, "stockRear", STOCK_REAR_TEETH);
  appendJsonField(json, "currentFront", currentFrontTeeth);
  appendJsonField(json, "currentRear", currentRearTeeth);
  appendJsonField(json, "stockFinal", stockFinalDrive, 4);
  appendJsonField(json, "currentFinal", currentFinalDrive, 4);
  appendJsonField(json, "correctionFactor", correctionFactor, 4);
  appendJsonField(json, "maxM", maxRecordedSpeed);
  appendJsonField(json, "maxR", maxRecordedRPM);
  appendJsonField(json, "b60", best0To60, 2);
  appendJsonField(json, "bQ", bestQuarterMile, 2);
  appendJsonField(json, "t60", time0To60, 2);
  appendJsonField(json, "t100", time0To100, 2);
  appendJsonField(json, "tQ", timeQuarterMile, 2);
  appendJsonField(json, "tS", speedQuarterMile);
  appendJsonField(json, "hrs", engineHours, 4);
  appendJsonField(json, "oil", oilLifePercent);
  appendJsonField(json, "oilM", milesOnCurrentOil, 2);
  appendJsonField(json, "oilH", hoursOnCurrentOil, 4);
  appendJsonField(json, "trA", tripA, 2);
  appendJsonField(json, "trB", tripB, 2);
  json.setCharAt(json.length() - 1, '}');

  server.send(200, "application/json", json);
}

void handleSetSprocket() {
  if (server.hasArg("idx")) {
    int idx = server.arg("idx").toInt();
    if (idx >= 0 && idx < 4) {
      currentSetupIndex = idx;
      userRestoredSetupIdx = idx;
      currentFrontTeeth = simulationSetups[idx].frontTeeth;
      currentRearTeeth = simulationSetups[idx].rearTeeth;
      isDemoModeActive = false;
      lastActivityTime = millis();
      server.send(200, "text/plain", "Error Success");
    } else {
      server.send(400, "text/plain", "Bad Sprocket Index");
    }
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void handleToggleSim() {
  if (server.hasArg("active")) {
    isSimulationActive = (server.arg("active") == "1");
    if (!isSimulationActive) { dragSimState = DRAG_IDLE; simStateTimer = 0; }
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void handleSetSource() {
  if (server.hasArg("mode")) {
    String mode = server.arg("mode");
    telemetrySource = (mode == "bike") ? SOURCE_BIKE_INPUTS : SOURCE_SIMULATOR;
    if (telemetrySource == SOURCE_BIKE_INPUTS) {
      isSimulationActive = false;
      dragSimState = DRAG_IDLE;
      simStateTimer = 0;
    }
    lastActivityTime = millis();
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void handleAdjustConfig() {
  if (server.hasArg("type") && server.hasArg("delta")) {
    String type = server.arg("type");
    int delta = server.arg("delta").toInt();
    lastActivityTime = millis(); 
    
    if (type == "shift") {
      userShiftPoint += delta;
      if (userShiftPoint > 15000) userShiftPoint = 5000;
      if (userShiftPoint < 5000) userShiftPoint = 15000;
    } 
    else if (type == "flash") {
      lightFlashDelay += delta;
      if (lightFlashDelay < 5) lightFlashDelay = 100;
      if (lightFlashDelay > 100) lightFlashDelay = 5;
    }
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void handleResetOil() {
  oilLifePercent = 100;
  hoursOnCurrentOil = 0.0;
  milesOnCurrentOil = 0.0;
  lastActivityTime = millis();
  server.send(200, "text/plain", "Registers Reset Successfully");
}

void handleResetTrip() {
  if (server.hasArg("meter")) {
    String meter = server.arg("meter");
    if (meter == "A") {
      tripA = 0.0;
      lastActivityTime = millis();
      server.send(200, "text/plain", "Trip A Reset Successfully");
    } else if (meter == "B") {
      tripB = 0.0;
      lastActivityTime = millis();
      server.send(200, "text/plain", "Trip B Reset Successfully");
    } else {
      server.send(400, "text/plain", "Bad Trip Meter");
    }
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void sendRaceResponse(const String &payload) {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", payload);
}

void handleRaceCreate() {
  raceHostToken = String(random(1000, 9999));
  racePartnerIp = "";
  raceState = RACE_HOSTING;
  raceStartTime = 0;

  String json = "{";
  json += "\"token\":\"" + raceHostToken + "\",";
  json += "\"status\":\"hosting\",";
  json += "\"hostIp\":\"" + WiFi.softAPIP().toString() + "\"";
  json += "}";
  sendRaceResponse(json);
}

void handleRaceJoin() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  if (!server.hasArg("token")) {
    server.send(400, "text/plain", "Missing token");
    return;
  }
  String token = server.arg("token");
  if (raceState != RACE_HOSTING || token != raceHostToken) {
    server.send(400, "text/plain", "Invalid token or not accepting joins");
    return;
  }

  racePartnerIp = server.client().remoteIP().toString();
  raceState = RACE_PAIRED;

  String json = "{";
  json += "\"status\":\"paired\",";
  json += "\"partner\":\"" + racePartnerIp + "\"";
  json += "}";
  sendRaceResponse(json);
}

void handleRaceStart() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  if (!server.hasArg("token") || server.arg("token") != raceHostToken) {
    server.send(400, "text/plain", "Invalid host token");
    return;
  }
  if (raceState != RACE_PAIRED) {
    server.send(400, "text/plain", "No paired race to start");
    return;
  }
  raceState = RACE_ACTIVE;
  raceStartTime = millis();
  // attempt to notify partner device to begin at the same time
  if (racePartnerIp.length() > 0) {
    WiFiClient client;
    String url = String("/raceNotify?token=") + raceHostToken;
    if (client.connect(racePartnerIp.c_str(), 80)) {
      client.print(String("GET ") + url + " HTTP/1.1\r\nHost: " + racePartnerIp + "\r\nConnection: close\r\n\r\n");
      // read and discard response briefly
      unsigned long start = millis();
      while (client.connected() && (millis() - start) < 400) {
        while (client.available()) client.read();
      }
      client.stop();
    }
  }
  String json = "{";
  json += "\"status\":\"active\"";
  json += "}";
  sendRaceResponse(json);
}

void handleRaceNotify() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  if (!server.hasArg("token") || server.arg("token") != raceHostToken) {
    server.send(400, "text/plain", "Invalid token");
    return;
  }
  // partner received notification to begin
  raceState = RACE_ACTIVE;
  raceStartTime = millis();
  String json = "{";
  json += "\"status\":\"notified\"";
  json += "}";
  sendRaceResponse(json);
}

void handleRaceCancel() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  if (server.hasArg("token") && server.arg("token") != raceHostToken) {
    server.send(400, "text/plain", "Invalid token");
    return;
  }
  raceState = RACE_IDLE;
  raceHostToken = "";
  racePartnerIp = "";
  raceStartTime = 0;
  String json = "{";
  json += "\"status\":\"idle\"";
  json += "}";
  sendRaceResponse(json);
}

void handleRaceStatus() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  String state = raceStateToString(raceState);
  String json = "{";
  json += "\"status\":\"" + state + "\",";
  json += "\"token\":\"" + raceHostToken + "\",";
  json += "\"partner\":\"" + racePartnerIp + "\",";
  json += "\"hostIp\":\"" + WiFi.softAPIP().toString() + "\"";
  json += "}";
  sendRaceResponse(json);
}

void drawHoldProgressBar(int yPos) {
  unsigned long holdProgress = millis() - buttonHoldStartTime;
  int barW = map(holdProgress, 0, UNLOCK_HOLD_TIME, 0, 124);
  barW = constrain(barW, 0, 124);
  display.drawRect(0, yPos, 128, 7, SSD1306_WHITE);
  display.fillRect(1, yPos + 1, barW, 5, SSD1306_WHITE);
}

void updateDisplay() {
  display.clearDisplay();
  
  if ((currentPage == 0 || currentPage == 8) && isSimulationActive && currentRPM >= userShiftPoint) {
    flashInverted = !flashInverted;
    display.invertDisplay(flashInverted);
  } else {
    display.invertDisplay(false); 
  }

  display.setTextSize(1); display.setTextColor(SSD1306_WHITE); display.setCursor(0, 0);
  
  if (currentPage == 0)      { display.println("CBR600RR COMPUTER"); }
  else if (currentPage == 2) { display.println("GEARING RATIO MATRIX"); }
  else if (currentPage == 3) { display.print("SPROCKET CONFIG "); display.println(isEditModeActive ? "[EDIT]" : "[NAV]"); } 
  else if (currentPage == 4) { display.println("PERFORMANCE TIMER"); }
  else if (currentPage == 5) { display.print("SHIFT POINT "); display.println(isEditModeActive ? "[EDIT]" : "[NAV]"); }
  else if (currentPage == 6) { display.print("MAINTENANCE LOG "); display.println(isEditModeActive ? "[EDIT]" : "[NAV]"); }
  else if (currentPage == 7) { display.println("WI-FI NETWORK PORTAL"); } 
  else if (currentPage == 8) { display.println("PERSONAL BESTS"); } 
  else if (currentPage == 9) { display.println("SIMULATION DEMO"); }
  else if (currentPage == 10){ display.print("SIMULATION CORE "); display.println(isEditModeActive ? "[EDIT]" : "[NAV]"); }
  else if (currentPage == 11){ display.print("ODOMETER "); display.println(isEditModeActive ? "[EDIT]" : "[NAV]"); }
  else if (currentPage == 12){ display.print("SHIFT LIGHTS "); display.println(isEditModeActive ? "[EDIT]" : "[NAV]"); }
  
  display.drawFastHLine(0, 10, 128, SSD1306_WHITE); 

  if (currentPage == 0) {
    display.setCursor(0, 20); display.setTextSize(3); display.print(currentMPH);
    display.setTextSize(1); display.println(" MPH");
    display.setCursor(75, 20); display.print("GEAR");
    display.setCursor(75, 32); display.setTextSize(3); display.print(currentGear);
    display.setCursor(0, 53); display.setTextSize(1); 
    
    if (isHoldingUnlockButton && !isMenuUnlocked) {
      display.print("UNLOCKING: ");
      drawHoldProgressBar(53);
    } else {
      display.print("RPM: "); display.print(currentRPM);
      display.setCursor(76, 53); 
      if (!isMenuUnlocked) display.print("SYS LCK");
    }
  } 
  else if (currentPage == 2) { 
    // GEARING RATIO MATRIX (moved to page 2)
    display.setCursor(0, 20);
    display.println("S1: 15/43 Ratio: 2.86");
    display.println("S2: 15/45 Ratio: 3.00");
    display.println("S3: 16/43 Ratio: 2.68"); 
    display.println("S4: 16/45 Ratio: 2.81");
    display.setCursor(0, 55); 
    if (isDemoModeActive) {
       display.print("ACTIVE: [DEMO CYCLE]");
    } else {
       display.print("ACTIVE PROFILE S"); display.print(currentSetupIndex + 1);
    }
  } 
  else if (currentPage == 3) { 
    if (isHoldingUnlockButton && !isEditModeActive) {
      display.setCursor(0, 22); display.println("Entering Setup...");
      drawHoldProgressBar(42);
    } else {
      display.setCursor(0, 18);
      for (int i = 0; i < 5; i++) {
        if (isEditModeActive && speedOffsetSelectIdx == i) display.print("> ");
        else if (!isEditModeActive && !isDemoModeActive && currentSetupIndex == i && i < 4) display.print("* ");
        else if (!isEditModeActive && isDemoModeActive && i == 4) display.print("* ");
        else display.print("  ");
        
        if (i < 4) {
          display.print("S"); display.print(i+1); display.print(": ");
          display.print(simulationSetups[i].frontTeeth); display.print("T/");
          display.print(simulationSetups[i].rearTeeth); display.print("T");
          if (currentSetupIndex == i && isDemoModeActive) display.print(" [RUN]");
          display.println();
        } else {
          display.println("DEMO CYCLE LOOP");
        }
      }
    }
  }
  else if (currentPage == 4) {
    display.setCursor(0, 16);
    if (currentTimerState == READY_TO_LAUNCH) {
      display.println("Status: BEST RECORDS");
      display.setCursor(0, 26); display.print("0-60 MPH:   "); display.print(best0To60, 2); display.println("s");
      display.print("0-100 MPH:  "); display.print(best0To100, 2); display.println("s");
      display.print("1/4 Mile:   "); display.print(bestQuarterMile, 2); display.println("s");
      display.print("Trap Speed: "); display.print(bestSpeedQuarterMile); display.println(" MPH");
    } 
    else if (currentTimerState == RACING) {
      display.println("Status: RACING RUN...");
      display.setCursor(0, 26); display.print("0-60:  "); 
      if(achieved60) { display.print(time0To60, 2); display.println("s"); } else { display.println("TRACKING"); }
      display.print("0-100: "); 
      if(achieved100) { display.print(time0To100, 2); display.println("s"); } else { display.println("TRACKING"); }
      display.print("Dist:  "); display.print(dragDistanceFeet, 0); display.println(" / 1320 ft");
    } 
    else if (currentTimerState == RUN_COMPLETE) {
      display.print("Status: TICKET ("); 
      display.print((TICKET_DISPLAY_DURATION - (millis() - runFinishTime)) / 1000); display.println("s)");
      display.setCursor(0, 26); display.print("0-60 MPH:   "); display.print(time0To60, 2); display.println("s");
      display.print("0-100 MPH:  "); display.print(time0To100, 2); display.println("s");
      display.print("1/4 Mile:   "); display.print(timeQuarterMile, 2); display.println("s");
      display.print("Trap Speed: "); display.print(speedQuarterMile); display.println(" MPH");
    }
  }
  else if (currentPage == 5) {
    if (isHoldingUnlockButton && !isEditModeActive) {
      display.setCursor(0, 22); display.println("Opening Config...");
      drawHoldProgressBar(42);
    } else {
      display.setCursor(0, 18); display.println("Flash RPM Trigger:");
      display.setCursor(0, 31); display.setTextSize(2);
      display.print(userShiftPoint); display.setTextSize(1); display.println(" RPM");
      display.setCursor(0, 51); 
      if(!isEditModeActive) display.println("Hold Top to Edit");
      else display.println("[Top] +100 | [Right] Exit");
    }
  }
  else if (currentPage == 6) {
    if (isHoldingUnlockButton && !isEditModeActive) {
      display.setCursor(0, 22); display.println("Accessing Engine...");
      drawHoldProgressBar(42);
    } else if (isEditModeActive) {
      display.setCursor(0, 18);
      display.println("> RESET OIL LIFE");
      
      display.setCursor(0, 36);
      if (isHoldingUnlockButton) {
        display.println("RESETTING REGISTERS...");
        drawHoldProgressBar(52);
      } else {
        display.println("Hold Right to Reset");
        display.println("Press Top to Back Out");
      }
    } else {
      display.setCursor(0, 16);
      display.print("Tot Hours: "); display.print(engineHours, 4); display.println(" hr");
      display.print("Oil Life : "); display.print(oilLifePercent); display.println("%");
      display.print("Oil Hour : "); display.print(hoursOnCurrentOil, 4); display.println(" hr");
      display.print("Oil Miles: "); display.print(milesOnCurrentOil, 1); display.println(" mi");
      
      display.setCursor(0, 56);
      display.print("Hold Top to Configure");
    }
  }
  else if (currentPage == 7) {
    display.setCursor(0, 18); 
    display.print("SSID:"); display.println(ssid); 
    display.print("IP:   192.168.4.1");
    display.print("Port: 80 (Active)");
  }
  else if (currentPage == 8) {
    display.setCursor(0, 16);
    display.print("Max Speed:  "); display.print(maxRecordedSpeed); display.println(" MPH");
    display.print("Max RPM:    "); display.print(maxRecordedRPM); display.println(" RPM");
    display.print("Best 0-60:  "); display.print(best0To60, 2); display.println(" s");
    display.print("Best 0-100: "); display.print(best0To100, 2); display.println(" s");
    display.print("Best 1/4M:  "); display.print(bestQuarterMile, 2); display.println(" s");
  }
  else if (currentPage == 9) {
    display.setCursor(0, 16);
    display.print("Profile: "); 
    if (isDemoModeActive) display.print("[D] ");
    display.print(simulationSetups[currentSetupIndex].frontTeeth); 
    display.print("T / "); display.print(simulationSetups[currentSetupIndex].rearTeeth); display.println("T");
    
    display.print("Stage: ");
    if (!isSimulationActive) display.println("HALTED");
    else if (dragSimState == DRAG_IDLE) display.println("10s IDLE HOLD");
    else if (dragSimState == DRAG_STAGE_ROLL) display.println("25ft STG ROLL");
    else if (dragSimState == DRAG_BURNOUT) display.println("BURNOUT LIMIT");
    else if (dragSimState == DRAG_PRE_LAUNCH) display.println("LAUNCH READY");
    else if (dragSimState == DRAG_RACE_WOT) display.println("1/4 MILE WOT");
    else if (dragSimState == DRAG_COMPLETE) display.println("RUN COMPLETE");

    display.print("RPM: "); display.print(currentRPM); display.print(" G: "); display.println(currentGear);
    display.print("Speed: "); display.print(currentMPH); display.println(" MPH");
  }
  else if (currentPage == 10) {
    if (isHoldingUnlockButton && !isEditModeActive) {
      display.setCursor(0, 22); display.println("Opening Core Logic...");
      drawHoldProgressBar(42);
    } else {
      display.setCursor(0, 18); display.print("Engine State: "); 
      display.setTextSize(2); display.println(isSimulationActive ? "ON" : "OFF");
      display.setTextSize(1);
      display.setCursor(0, 53);
      if (!isEditModeActive) display.println("Hold Top to Edit");
      else display.println("[Top] Toggle | [Right] Exit");
    }
  }
  else if (currentPage == 11) { 
    if (isHoldingUnlockButton && !isEditModeActive) {
      display.setCursor(0, 22); display.println("Opening Registers...");
      drawHoldProgressBar(42);
    } else {
      display.setCursor(0, 16); display.print("Odometer: "); display.print(totalOdometer, 1); display.println(" mi");
      if (isEditModeActive && odometerSelectRow == 0) display.print("> "); else display.print("  ");
      display.print("Trip A:   "); display.print(tripA, 2); display.println(" mi");
      if (isEditModeActive && odometerSelectRow == 1) display.print("> "); else display.print("  ");
      display.print("Trip B:   "); display.print(tripB, 2); display.println(" mi");
      display.setCursor(0, 53);
      if (!isEditModeActive) display.println("Hold Top to Edit");
      else display.println("[Top] Reset | [Right] Swap");
    }
  }
  else if (currentPage == 12) { 
    if (isHoldingUnlockButton && !isEditModeActive) {
      display.setCursor(0, 22); display.println("Opening Timers...");
      drawHoldProgressBar(42);
    } else {
      display.setCursor(0, 18); display.println("Light Flash Rate:");
      display.setCursor(0, 31); display.setTextSize(2);
      display.print(lightFlashDelay); display.setTextSize(1); display.println(" ms");
      display.setCursor(0, 51); 
      if(!isEditModeActive) display.println("Hold Top to Edit");
      else display.println("[Top] -5ms | [Right] Exit");
    }
  }
  display.display();
}

void setup() {
  Serial.begin(115200);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) for(;;);

  pinMode(BUTTON_TOP, INPUT_PULLUP);
  pinMode(BUTTON_RIGHT, INPUT_PULLUP);
  pinMode(PIN_SPEED_IN, INPUT_PULLUP);
  pinMode(PIN_RPM_IN, INPUT_PULLUP);
  pinMode(PIN_NEUTRAL_IN, INPUT_PULLUP);
  pinMode(PIN_KICKSTAND_IN, INPUT_PULLUP);
  pinMode(PIN_SPEED_OUT, OUTPUT);
  digitalWrite(PIN_SPEED_OUT, LOW);

  ledcSetup(SPEED_OUT_CHANNEL, 1000, 8);
  ledcAttachPin(PIN_SPEED_OUT, SPEED_OUT_CHANNEL);
  ledcWriteTone(SPEED_OUT_CHANNEL, 0);

  attachInterrupt(digitalPinToInterrupt(PIN_SPEED_IN), onSpeedPulse, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_RPM_IN), onRPMPulse, FALLING);
  
  WiFi.softAP(ssid, password);
  
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/toggleSim", handleToggleSim); 
  server.on("/setSource", handleSetSource);
  server.on("/setSprocket", handleSetSprocket);
  server.on("/adjustConfig", handleAdjustConfig);
  server.on("/resetOil", handleResetOil);
  server.on("/resetTrip", handleResetTrip);
  server.on("/raceCreate", handleRaceCreate);
  server.on("/raceJoin", handleRaceJoin);
  server.on("/raceStart", handleRaceStart);
  server.on("/raceNotify", handleRaceNotify);
  server.on("/raceCancel", handleRaceCancel);
  server.on("/raceStatus", handleRaceStatus);
  server.begin();
  
  simStateTimer = millis();
  lastSimUpdate = millis();
  lastActivityTime = millis();
}

void loop() {
  server.handleClient();
  runAdvancedCBRSimulation();
  updateDisplay(); 

  unsigned long timeSinceLastInput = millis() - lastActivityTime;

  if (isEditModeActive && timeSinceLastInput > EDIT_MODE_TIMEOUT) {
    isEditModeActive = false;
    isHoldingUnlockButton = false;
  }
  if (isMenuUnlocked && currentPage != 0 && !isEditModeActive && timeSinceLastInput > PAGE_HOME_TIMEOUT) {
    currentPage = 0; 
  }
  if (isMenuUnlocked && timeSinceLastInput > MASTER_LOCK_TIMEOUT) {
    isMenuUnlocked = false; isEditModeActive = false; currentPage = 0;
  }

  bool topBtnPressed = (digitalRead(BUTTON_TOP) == LOW);
  bool rightBtnPressed = (digitalRead(BUTTON_RIGHT) == LOW);

  if (!topBtnPressed) topButtonWasReleased = true;
  if (!rightBtnPressed) rightButtonWasReleased = true;

  if (topBtnPressed && rightBtnPressed) {
    if (isEditModeActive) {
      isEditModeActive = false; isHoldingUnlockButton = false;
      topButtonWasReleased = false; rightButtonWasReleased = false;
      lastDoublePressTime = millis(); lastActivityTime = millis();
      delay(400); 
    }
    return;
  }

  if (!isMenuUnlocked && currentPage == 0) {
    if (topBtnPressed || rightBtnPressed) { 
      if (!isHoldingUnlockButton) {
        isHoldingUnlockButton = true;
        buttonHoldStartTime = millis();
      } else if (millis() - buttonHoldStartTime >= UNLOCK_HOLD_TIME) {
        isMenuUnlocked = true; isHoldingUnlockButton = false; currentPage = 2; 
        topButtonWasReleased = false; rightButtonWasReleased = false;
        lastActivityTime = millis(); delay(300);
      }
    } else {
      isHoldingUnlockButton = false;
    }
    return;
  }

  if (isMenuUnlocked && (millis() - lastButtonCheck > 50)) {
     if (!isEditModeActive && topBtnPressed && topButtonWasReleased && 
       (currentPage == 3 || currentPage == 5 || currentPage == 6 || currentPage == 10 || currentPage == 11 || currentPage == 12)) {
      
      if (!isHoldingUnlockButton) {
        isHoldingUnlockButton = true;
        buttonHoldStartTime = millis();
      } else if (millis() - buttonHoldStartTime >= UNLOCK_HOLD_TIME) {
        isEditModeActive = true;
        isHoldingUnlockButton = false;
        topButtonWasReleased = false; 
        lastActivityTime = millis();
        
        if (currentPage == 3) speedOffsetSelectIdx = isDemoModeActive ? 4 : currentSetupIndex;
        if (currentPage == 11) odometerSelectRow = 0;
        delay(300);
      }
      return; 
    }

    if (!isEditModeActive && !topBtnPressed && isHoldingUnlockButton) {
      isHoldingUnlockButton = false;
    }

    if (isEditModeActive && currentPage == 5) {
      lastActivityTime = millis();
      if (topBtnPressed && topButtonWasReleased) {
        isEditModeActive = false; isHoldingUnlockButton = false;
        topButtonWasReleased = false; delay(200); return;
      }
      if (rightBtnPressed) {
        if (!isHoldingUnlockButton) {
          isHoldingUnlockButton = true;
          buttonHoldStartTime = millis();
        } else if (millis() - buttonHoldStartTime >= UNLOCK_HOLD_TIME) {
          oilLifePercent = 100; hoursOnCurrentOil = 0.0; milesOnCurrentOil = 0.0;
          isEditModeActive = false; isHoldingUnlockButton = false;
          rightButtonWasReleased = false; delay(300);
        }
      } else {
        isHoldingUnlockButton = false;
      }
      return; 
    }

    if (topBtnPressed && topButtonWasReleased && !rightBtnPressed) {
      if (millis() - lastDoublePressTime < DEBOUNCE_CUSHION) return; 
      lastActivityTime = millis(); topButtonWasReleased = false; 
      
      if (isEditModeActive) {
        if (currentPage == 3) { 
          if (speedOffsetSelectIdx == 4) { isDemoModeActive = true; } 
          else { isDemoModeActive = false; currentSetupIndex = speedOffsetSelectIdx; userRestoredSetupIdx = currentSetupIndex; }
          isEditModeActive = false; 
        }
        else if (currentPage == 5) {
          userShiftPoint += 100;
          if (userShiftPoint > 15000) userShiftPoint = 5000; 
        }
        else if (currentPage == 10) {
          isSimulationActive = !isSimulationActive; 
          if (!isSimulationActive) { dragSimState = DRAG_IDLE; simStateTimer = 0; currentSetupIndex = userRestoredSetupIdx; }
        }
        else if (currentPage == 11) { 
          if (odometerSelectRow == 0) tripA = 0.0; else if (odometerSelectRow == 1) tripB = 0.0;
        }
        else if (currentPage == 12) { 
          lightFlashDelay -= 5;
          if (lightFlashDelay < 5) lightFlashDelay = 100;
        }
      }
      lastButtonCheck = millis();
    }

    if (rightBtnPressed && rightButtonWasReleased && !topBtnPressed) {
      if (millis() - lastDoublePressTime < DEBOUNCE_CUSHION) return; 
      lastActivityTime = millis(); rightButtonWasReleased = false; 
      
        if (isEditModeActive) {
        if (currentPage == 3) { speedOffsetSelectIdx = (speedOffsetSelectIdx + 1) % 5; }
        else if (currentPage == 11) { odometerSelectRow = (odometerSelectRow + 1) % 2; } 
        else { isEditModeActive = false; }
      } else {
        currentPage = (currentPage + 1) % TOTAL_PAGES; 
      }
      lastButtonCheck = millis();
    }
  }
}
