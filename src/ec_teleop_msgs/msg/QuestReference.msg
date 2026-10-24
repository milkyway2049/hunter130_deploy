# A delayed, measured 10-frame reference horizon at 50 Hz; never motor commands.
uint8 VERSION=1
std_msgs/Header header
uint8 protocol_version
uint64 session_id
uint64 sequence
bool enabled
bool calibrated
float64 source_age
float64 sample_dt
string[25] joint_names
geometry_msgs/Pose[10] root_poses
geometry_msgs/Twist[10] root_velocities
float64[250] joint_positions
float64[250] joint_velocities
