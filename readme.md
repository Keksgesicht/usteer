# Usteer Documentation

## Commandline Parameters

 Usteer can be configured with the following command-line arguments:

| Parameter Name | Description | Default Value | Value Range |
|----------------|-------------|---------------|-------------|
| `-v` | Increases the console debug logging level. Repeated use increases level further. Levels: 1-Info messages, 2-Debug messages, 3-Verbose Debug, 4-Include Network messages, 5-Include extra testing messages | `0` |  `0-5` |
| `-i <name>`  | Connect to other instance on interface `<name>` | `none` | `string` |
| `-s` | Output log messages to syslog instead of stderr | `false` | `true/false` |
<br>

## Usteer Configuration

 Usteer can be configured with the following parameters:

| Parameter Name | Description | Default Value | Value Range |
|----------------|-------------|---------------|-------------|
| `syslog` | When set to true, usteer will output the log messages to the syslog insted of the default, stderr. | `false` |  `true/false` |
| `debug_level` | The debug level determines which log messages are generated. The higher the debug level, the more messages are generated. See function 'usage' in main.c for the possible debug levels and their included messages. | `0` |  `0-5` |
| `sta_block_timeout` | Timeout after a station timeout is resetted. | `30k` |  `unsigned 32 bit int` |
| `local_sta_timeout` | Timeout after a station is considered timeouted. | `120k` |  `unsigned 32 bit int` |
| `local_sta_update` | Time interval in which usteer sets a timer for a station update timeout. | `1k` |  `unsigned 32 bit int` |
| `max_retry_band` | Max amount of retries before an event or request from a station is treated with urgency. | `5` |  `unsigned 32 bit int` |
| `seen_policy_timeout` | This value determines the size of the time interval after a station will not be considered a better candidate. When checking for a better candidate, a time delta between the current time and the 'seen' value is compared. If the value is greater than this value, the station will not be considered a better candidate at all. | `30k` |  `unsigned 32 bit int` |
| `band_steering_threshold` | This threshold is used to calculate a metric between a current and new station. If the current station operates on 5GHz, but the new station does not, this value is added on the side of the new station. If the current station operates on 2.4GHz, the value is added for the current station. At the end of the day, this value represents a penalty that is taken into consideration which station of the two is better. The higher this value, the higher the penalty if a  station operates on a lower frequency. | `5` |  `unsigned 32 bit int` |
| `load_balancing_threshold` | Similarily like 'band_steering_threshold', this value is a penalty that most probably models if it is viable to roam a client to another station by taking the generated overhead and traffic generated into consideration. The higher this value is, the higher the penalty is when determening if another station is better for a client. | `5` |  `unsigned 32 bit int` |
| `remote_update_interval` | How frequently usteer updates remote information. | `1k` |  `unsigned 32 bit int` |
| `remote_node_timeout` | Time until usteer consideres a remote node as timeouted and removes it from it's known nodes. | `120k` |  `unsigned 32 bit int` |
| `min_snr` | Signal-noise-ratio. Currently not used. This value is used as a threshold that determines at what signal noise ratio a local node is kicked from a station. | `0` |  `signed 32 bit int` |
| `min_connect_snr` | Minimum signal-to-noise ratio so that a client request is accepted. | `0` |  `signed 32 bit int` |
| `signal_diff_threshold` | Threshold how much better the signal strength of a new node has to be so usteer determines it as a better signal. A signal is better if: `new_signal - current_signal > signal_diff_threshold` | `0` |  `unsigned 32 bit int` |
| `roam_scan_snr` | The threshold Signal-Noise-Ratio after useteer starts a roam scan. | `0` |  `signed 32 bit int` |
| `roam_scan_tries` | The amount of attempts a station should take to attempt to roam before kicking. | `3` |  `1 - max unsigned 32 bit int` |
| `roam_scan_interval` | This value defines the frequency usteer scans for roaming possibilities. | `10k` |  `unsigned 32 bit int` |
| `roam_trigger_snr` | The threshold Signal-Noise-Ratio after usteer attempts to roam a client. | `0` |  `signed 32 bit int` |
| `roam_trigger_interval` | This value defines the frequency usteer attempts to roam clients. | `60k` |  `unsigned 32 bit int` |
| `roam_kick_delay` | Delay before a client is kicked if the client fails to roam in time. | `100` |  `unsigned 32 bit int` |
| `initial_connect_delay` | The time in milliseconds usteer ignores requestes from a station after it was created. | `0` |  `unsigned 32 bit int` |
| `load_kick_enabled` | When enabled, nodes that exceed the 'load_kick_threshold' will be automatically kicked. | `false` |  `true/false` |
| `load_kick_threshold` | The threshold a node has to exceed in order to be load-kicked. | `75` |  `0 - 100` |
| `load_kick_delay` | Delay that usteer waits before load-kicking a client. | `10.000` |  `unsigned 32 bit int` |
| `load_kick_min_clients` | When load-kicking is enabled, this property determines at which point a node stops to load-kick clients based on the amount of connected clients. If the number of connected clients is less than this property, no clients will be kicked even if they are over the load-threshold. | `10` |  `unsigned 32 bit int` |
| `load_kick_reason_code` | The reason why a client was load-kicked. Default is WLAN_REASON_DISASSOC_AP_BUSY (5) | `5` |  `802.11-2016 Table 9-45 Reason codes ` |
| `kick_client_active_sec` | The time interval in which the client transfered bits are measured. Raising this value during runtime will only affect clients that connect after the change. Lowering it will only waste some bits until they disconnect. | `30` | `unsigned 32 bit int` |
| `kick_client_active_kbits` | How many kilobits per second (average over the time above) the client needs to transfer without getting kicked | `50` | `unsigned 32 bit int` |
| `node_up_script` | executable that is executed after the usteer node starts up. | `0` |  `string` |
<br>
