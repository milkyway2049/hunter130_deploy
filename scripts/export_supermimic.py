#!/usr/bin/env python3
"""Export a Hunter130 SuperMimic actor and prepare its deployment motion CSVs.

Run using the training project's Python environment. Training dependencies are
needed only for export; the deployed controller uses C++ and ONNX Runtime.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import sys

import numpy as np
from scipy.spatial.transform import Rotation
import torch


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def convert_motion(source, destination, names, lower, upper):
    """64 columns: time, root pose, q, qd, root linear/world angular velocity.

    NPZ input preserves the exact training reference velocities. Legacy 33-column
    deployment CSV input derives velocities by finite differences at 50 Hz.
    """
    if source.suffix == '.npz':
        with np.load(source) as d:
            assert list(d['joint_names']) == names and np.isclose(float(d['fps']), 50)
            pelvis = list(d['body_names']).index('pelvis')
            pos, quat = d['body_pos_w'][:, pelvis], d['body_quat_w'][:, pelvis]
            q, qd = d['joint_pos'], d['joint_vel']
            lin, ang = d['body_lin_vel_w'][:, pelvis], d['body_ang_vel_w'][:, pelvis]
            time = np.arange(len(q)) * .02
    else:
        with source.open() as f:
            reader = csv.reader(f)
            header = next(reader)
        core = ['time', 'root_tx', 'root_ty', 'root_tz', 'root_qw', 'root_qx', 'root_qy', 'root_qz'] + names
        assert header[:33] == core, 'Motion joint order does not match the model'
        data = np.loadtxt(source, delimiter=',', skiprows=1)
        assert data.ndim == 2 and data.shape[1] in (33, 64) and len(data) >= 3
        time, pos, quat, q = data[:, 0], data[:, 1:4], data[:, 4:8], data[:, 8:33]
        if data.shape[1] == 64:
            assert header == motion_header(names)
            qd, lin, ang = data[:, 33:58], data[:, 58:61], data[:, 61:64]
        else:
            qd, lin = np.gradient(q, .02, axis=0), np.gradient(pos, .02, axis=0)
            rot = Rotation.from_quat(quat[:, [1, 2, 3, 0]])
            delta = (rot[1:] * rot[:-1].inv()).as_rotvec() / .02
            ang = np.vstack([delta[0], .5 * (delta[:-1] + delta[1:]), delta[-1]])
    data = np.column_stack([time, pos, quat, q, qd, lin, ang])
    assert np.isfinite(data).all() and len(data) >= 3
    assert np.allclose(time, np.arange(len(data)) * .02, atol=1e-6, rtol=0)
    assert np.allclose(np.linalg.norm(quat, axis=1), 1, atol=.001, rtol=0)
    assert (q >= np.asarray(lower) - 1e-5).all() and (q <= np.asarray(upper) + 1e-5).all()
    source_hash = sha256(source)
    destination.parent.mkdir(parents=True, exist_ok=True)
    np.savetxt(destination, data, delimiter=',', header=','.join(motion_header(names)), comments='', fmt='%.17g')
    return dict(source=str(source.resolve()), source_sha256=source_hash,
                path=destination.name, sha256=sha256(destination), frames=len(data),
                velocities='training_npz' if source.suffix == '.npz' else 'csv_50hz')


def motion_header(names):
    return (['time', 'root_tx', 'root_ty', 'root_tz', 'root_qw', 'root_qx', 'root_qy', 'root_qz']
            + names + ['velocity_' + n for n in names]
            + ['root_vx', 'root_vy', 'root_vz', 'root_wx', 'root_wy', 'root_wz'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--training-root', type=Path, required=True)
    parser.add_argument('--checkpoint', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True, help='ONNX output path')
    parser.add_argument('--motion', type=Path, action='append', default=[], help='Input motion CSV or NPZ')
    parser.add_argument('--motion-dir', type=Path, help='Directory for converted CSVs')
    args = parser.parse_args()
    sys.path.insert(0, str(args.training_root.resolve() / 'src'))
    from supermimic.policy.nets import Policy, PolicyCfg
    from supermimic.robots import load_model, robot_cfg, DEFAULT_HUNTER_ROOT
    torch.set_num_threads(4)
    checkpoint_hash = sha256(args.checkpoint)
    ck = torch.load(args.checkpoint, map_location='cpu', weights_only=False)
    cfg = ck['config']
    assert cfg['env']['robot'] == 'hunter130' and cfg['env']['anchor_obs']
    assert cfg['policy']['history'] == 10 and cfg['policy']['num_joints'] == 25
    assert cfg['env']['future_frames'] == 10 and cfg['policy']['goal_dim'] == 609
    pcfg = PolicyCfg(**{k: tuple(v) if isinstance(v, list) else v for k, v in cfg['policy'].items()})
    actor = Policy(pcfg).eval()
    actor.load_state_dict(ck['policy'])
    asset_root = cfg['env'].get('asset_root') or DEFAULT_HUNTER_ROOT
    model = load_model('hunter130', asset_root)
    entity, scale, _ = robot_cfg('hunter130', asset_root)
    names = [model.joint(i).name for i in range(1, model.njnt)]
    defaults = []
    for name in names:
        matches = [v for pattern, v in entity.init_state.joint_pos.items() if re.fullmatch(pattern, name)]
        assert len(matches) <= 1
        defaults.append(float(matches[0]) if matches else 0.)
    motors = json.loads((Path(asset_root) / 'assets/motors.json').read_text())
    # This controller implements the trained pelvis -> torso Z-X-Y chain.
    chain = ['waist_yaw_joint', 'waist_roll_joint', 'waist_pitch_joint']
    axes = [[0., 0., 1.], [1., 0., 0.], [0., 1., 0.]]
    parent = model.body('pelvis').id
    for name, axis in zip(chain, axes):
        j = model.joint(name).id
        b = int(model.jnt_bodyid[j])
        assert int(model.body_parentid[b]) == parent
        assert np.allclose(model.body_quat[b], [1, 0, 0, 0])
        assert np.allclose(model.jnt_axis[j], axis)
        parent = b
    assert parent == model.body('torso_link').id
    metadata = dict(format_version=2, policy_family='supermimic', position_feedback='zero',
        joint_names=names, default_joint_pos=defaults,
        kp=[motors[n]['stiffness'] for n in names], kd=[motors[n]['damping'] for n in names],
        action_scale=[scale[n] for n in names],
        joint_lower=model.jnt_range[1:, 0].tolist(), joint_upper=model.jnt_range[1:, 1].tolist(),
        history_length=10, future_frames=10, control_dt=.02,
        torso_joint_names=chain, torso_joint_axes=axes,
        observation_layout='term_major_oldest_to_newest',
        goal_layout='10x60_then_zero_position3_then_torso_rotation_first_two_columns_row_major6',
        model_inputs=dict(policy=[1, 810], goal=[1, 609]), model_outputs=dict(action=[1, 25]),
        scaling='included_in_onnx', source_checkpoint=str(args.checkpoint.resolve()),
        source_checkpoint_sha256=checkpoint_hash, iteration=int(ck['iteration']))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    torch.onnx.export(actor, (torch.zeros(1, 810), torch.zeros(1, 609)), str(args.output),
        input_names=['policy', 'goal'], output_names=['action'], opset_version=18,
        dynamo=False, external_data=False)
    import onnx
    onnx.checker.check_model(str(args.output))
    metadata['model_sha256'] = sha256(args.output)
    metadata['motions'] = []
    for motion in args.motion:
        destination = (args.motion_dir or motion.parent) / (motion.stem + '.csv')
        metadata['motions'].append(convert_motion(motion, destination, names,
            metadata['joint_lower'], metadata['joint_upper']))
    args.output.with_suffix('.json').write_text(json.dumps(metadata, indent=2) + '\n')
    print(json.dumps(dict(model=str(args.output), iteration=ck['iteration'], motions=metadata['motions']), indent=2))


if __name__ == '__main__':
    main()
