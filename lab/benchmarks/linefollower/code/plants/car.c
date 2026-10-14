#include <tracelab.h>
#include <tracelab_track.h>

/* The line follower's car (the port of line_follower_simulator's
 * car_modeling/car_dynamics.py) and its sensors, on the track environment.
 *
 * MOTORS, in % of PWM, discrete first order (z transform of 1/(tau s + 1)):
 *     m(k+1) = a m(k) + b sat(u(k)) + c q(k)
 *     a = exp(-Ts / tau), tau = settling time / 5, b = ke (1 - a), c = kq (1 - a)
 * q is the disturbance of sim.disturb (the scene's P / O keys): +q on the
 * left motor, -q on the right. The wheel speed is m * w1, w1 = 2 pi rpm /
 * 6000: the wheel's rad/s for 1 % of PWM.
 *
 * KINEMATICS, with the new wheel speeds and the old heading:
 *     v = (m_l + m_r) w1 r / 2,   omega = (m_l - m_r) w1 r / L   (> 0 turns right)
 *     x(k+1) = x + Ts v cos(theta),  y(k+1) = y + Ts v sin(theta),
 *     theta(k+1) = theta + Ts omega
 *
 * SENSORS: what the controller reads (loop.state).
 *     w_l, w_r  encoders: the wheel angle integrates the speed; the count is
 *               floor(angle / (2 pi / ticks)) and the speed is the count's
 *               change over Ts -- whole ticks only
 *     gyro      the yaw rate (+ white noise and a random-walk bias)
 *     accel_x   the accelerometer at the axle, along the car: the change of
 *               the speed over the last step, dv/dt
 *     accel_y   across the car: the centripetal acceleration v * omega, > 0
 *               when turning right (the sign of omega). Both with white
 *               noise and a random-walk bias of their own, like the gyro;
 *               their true values are the diagnostics accel_x_true and
 *               accel_y_true (speed and omega are the true v and omega,
 *               gyro_bias_true the gyro's bias)
 *     s[]       the line sensor array: N sensors `line_spacing` apart,
 *               `line_distance` ahead of the axle, s[0] the leftmost. Each
 *               sees a disc of `footprint` on the floor and reads the
 *               fraction of it the line covers, 0..1; then a 12-bit ADC.
 * With noise off this is the C++ plant of TraceLab's line-follower
 * benchmark, sample for sample (which has no accelerometer). The
 * accelerometer's noise is drawn after every other sensor's, so the gyro and
 * the line sensors read the same noise with it as without it.
 */

TL_DISCRETE;

/* The car (config.py "car" and "motors"). */
TL_PARAM(wheel_radius, 0.04, "m", 0.001, 1);
TL_PARAM(wheel_distance, 0.15, "m", 0.01, 2);
TL_PARAM(wheel_rpm, 1000, "rpm", 1, 1e5);
TL_PARAM(ke_left, 1, "", 0, 10);
TL_PARAM(ke_right, 1, "", 0, 10);
TL_PARAM(kq, 1, "", 0, 10);                   /* disturbance gain */
TL_PARAM(settle_left, 0.62, "s", 0.001, 10);  /* 5 tau */
TL_PARAM(settle_right, 0.58, "s", 0.001, 10);

/* The sensors. */
TL_PARAM_INT(encoder_ticks, 1024, 0, 1000000); /* per revolution; 0: exact speed */
TL_PARAM(line_spacing, 0.008, "m", 0.001, 0.05);
TL_PARAM(line_distance, 0.15, "m", 0, 0.5);
TL_PARAM(footprint, 0.004, "m", 0, 0.03);
TL_PARAM_INT(adc_bits, 12, 0, 24);            /* 0: no ADC */
TL_PARAM_BOOL(noise, 0);
TL_PARAM(gyro_white, 0.02, "rad/s", 0, 10);
TL_PARAM(gyro_walk, 0.002, "rad/s/sqrt(s)", 0, 10);
TL_PARAM(line_white, 0.02, "", 0, 1);
TL_PARAM(accel_white, 0.05, "m/s^2", 0, 100);
TL_PARAM(accel_walk, 0.005, "m/s^2/sqrt(s)", 0, 100);

#define N 15                                  /* line sensors */

TL_INPUT(u_l, "%", -100, 100);
TL_INPUT(u_r, "%", -100, 100);

TL_STATE(motor_l, "%", 0);                    /* motor outputs (wheel speed / w1) */
TL_STATE(motor_r, "%", 0);
TL_STATE(x, "m", 0);                          /* world pose: the environment */
TL_STATE(y, "m", 0);                          /* places it on the start */
TL_STATE(theta, "rad", 0);

TL_OUTPUT(w_l, "rad/s");
TL_OUTPUT(w_r, "rad/s");
TL_OUTPUT(gyro, "rad/s");
TL_OUTPUT_ARRAY(s, N, "");
TL_OUTPUT(accel_x, "m/s^2");                  /* after s[]: the fields before keep their place */
TL_OUTPUT(accel_y, "m/s^2");

TL_DIAG(speed, "m/s");
TL_DIAG(omega, "rad/s");
TL_DIAG(accel_x_true, "m/s^2");
TL_DIAG(accel_y_true, "m/s^2");
TL_DIAG(gyro_bias_true, "rad/s");             /* the gyro's random-walk bias */

static double angle[2];                       /* encoders */
static long long count[2];
static double gyro_bias;
static double accel_bias[2];
static double last_speed;                     /* the accelerometer's dv */

static double w1(void) {
    return (2 * M_PI * wheel_rpm) / (60 * 100);
}

void plant_reset(void) {
    angle[0] = angle[1] = 0;
    count[0] = count[1] = 0;
    gyro_bias = 0;
    accel_bias[0] = accel_bias[1] = 0;
    last_speed = 0;
    speed = omega = 0;
    accel_x_true = accel_y_true = 0;
    gyro_bias_true = 0;
}

void plant_update(void) {
    double a_l = exp(-tl_ts() / (settle_left / 5));
    double a_r = exp(-tl_ts() / (settle_right / 5));
    double q_l = tl_disturb(0), q_r = tl_disturb(1);
    double m_l = a_l * motor_l + ke_left * (1 - a_l) * tl_sat(u_l, -100, 100) + kq * (1 - a_l) * q_l;
    double m_r = a_r * motor_r + ke_right * (1 - a_r) * tl_sat(u_r, -100, 100) + kq * (1 - a_r) * q_r;
    next(motor_l) = m_l;
    next(motor_r) = m_r;

    double v = (m_l + m_r) * (w1() * wheel_radius / 2);
    double w = (m_l - m_r) * (w1() * wheel_radius / wheel_distance);
    double step = v * tl_ts();
    next(x) = x + step * cos(theta);
    next(y) = y + step * sin(theta);
    next(theta) = theta + w * tl_ts();
    speed = v;
    omega = w;
}

/* The area of a disc of radius r left of the vertical line at a (centre 0). */
static double disc_left_of(double a, double r) {
    if (a <= -r) return 0.0;
    if (a >= r) return M_PI * r * r;
    return r * r * acos(-a / r) + a * sqrt(r * r - a * a);
}

/* The fraction of a sensor's footprint the line covers. */
static double line_reading(double distance) {
    double w = tl_track_line_half_width();
    double r = 0.5 * footprint;
    if (!isfinite(distance)) return 0.0;
    if (r <= 0.0) return distance <= w ? 1.0 : 0.0;
    double value = (disc_left_of(w - distance, r) - disc_left_of(-w - distance, r)) / (M_PI * r * r);
    return tl_sat(value, 0.0, 1.0);
}

void plant_output(void) {
    /* Encoders: whole ticks. */
    double wheel[2] = {motor_l * w1(), motor_r * w1()};
    double out[2];
    for (int i = 0; i < 2; i++) {
        if (encoder_ticks <= 0) {
            out[i] = wheel[i];
            continue;
        }
        double per_tick = 2.0 * M_PI / encoder_ticks;
        angle[i] += wheel[i] * tl_ts();
        long long c = (long long)floor(angle[i] / per_tick);
        out[i] = (double)(c - count[i]) * per_tick / tl_ts();
        count[i] = c;
    }
    w_l = out[0];
    w_r = out[1];

    /* Gyro: the yaw rate of the last step. */
    double yaw = (motor_l - motor_r) * (w1() * wheel_radius / wheel_distance);
    gyro = yaw;
    if (noise) {
        gyro_bias += gyro_walk * sqrt(tl_ts()) * tl_noise(1.0);
        gyro = yaw + gyro_bias + gyro_white * tl_noise(1.0);
    }
    gyro_bias_true = gyro_bias;

    /* Line sensors: where each one is on the floor, how much line it sees. */
    double c = cos(theta), sn = sin(theta);
    double lsb = adc_bits > 0 ? 1.0 / (ldexp(1.0, adc_bits) - 1.0) : 0.0;
    for (int i = 0; i < N; i++) {
        double lateral = (i - 0.5 * (N - 1)) * line_spacing;
        double px = x - lateral * sn + line_distance * c;
        double py = y + lateral * c + line_distance * sn;
        double v = line_reading(tl_track_distance(px, py));
        if (noise) v += line_white * tl_noise(1.0);
        if (adc_bits > 0) {
            v = tl_sat(v, 0.0, 1.0);
            v = lsb * round(v / lsb);
        }
        s[i] = v;
    }

    /* Accelerometer: along the car, the speed's change over the step;
     * across it, the centripetal v * omega. Its noise last (see above). */
    double v_now = (motor_l + motor_r) * (w1() * wheel_radius / 2);
    accel_x_true = (v_now - last_speed) / tl_ts();
    accel_y_true = v_now * yaw;
    last_speed = v_now;
    accel_x = accel_x_true;
    accel_y = accel_y_true;
    if (noise) {
        for (int i = 0; i < 2; i++) {
            accel_bias[i] += accel_walk * sqrt(tl_ts()) * tl_noise(1.0);
        }
        accel_x += accel_bias[0] + accel_white * tl_noise(1.0);
        accel_y += accel_bias[1] + accel_white * tl_noise(1.0);
    }
}
