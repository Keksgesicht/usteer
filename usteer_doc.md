# Usteer Documentation 
 
## Configuration Parameters 
 
| Parameter Name | Description | Default Value | Value Range | 
|----------------|-------------|---------------|-------------| 
|   |   |   |   |   | 
|   |   |   |   |   | 
|   |   |   |   |   |

## Commandline Parameters

 Usteer can be configured with the following command-line arguments:

| Parameter Name | Description | Default Value | Value Range |
|----------------|-------------|---------------|-------------|
| `-v` | Increases the console debug logging level. Repeated use increases level further. Levels: 1-Info messages, 2-Debug messages, 3-Verbose Debug, 4-Include Network messages, 5-Include extra testing messages | `0` |  `0-5` |
| `-i <name>`  | Connect to other instance on interface `<name>` | `none` | `? (ANY)` |
| `-s` | Output log messages to syslog instead of stderr | `false` | `true/false` |

## Usteer Configuration

 Usteer can be configured with the following parameters:

| Parameter Name | Description | Default Value | Value Range |
|----------------|-------------|---------------|-------------|
| `initial_connect_delay` | The time in milliseconds usteer ignores requestes from a station after it was created. | `0` |  `0-...` |
| `load_kick_enabled` | When enabled, nodes that exceed the 'load_kick_threshold' will be automatically kicked. | `false` |  `true/false` |
| `load_kick_threshold` | The threshold a node has to exceed in order to be load-kicked. | `75` |  `0-... (100)` |
| `load_kick_delay` | ? | `10.000` |  `0-...` |
| `load_kick_min_clients` | When load-kicking is enabled, this property determines at which point a node stops to load-kick clients based on the amount of connected clients. If the number of connected clients is less than this property, no clients will be kicked even if they are over the load-threshold. | `10` |  `0-...` |
| `load_kick_reason_code` | The reason why a client was load-kicked. Default is WLAN_REASON_DISASSOC_AP_BUSY (5) | `5` |  `? (ANY)` |
