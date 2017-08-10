#include <math.h>
#include <uWS/uWS.h>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include "Eigen-3.3/Eigen/Core"
#include "Eigen-3.3/Eigen/QR"
#include "Eigen-3.3/Eigen/Dense"
#include "MPC.h"
#include "json.hpp"

// for convenience
using json = nlohmann::json;

// For converting back and forth between radians and degrees.
constexpr double pi() { return M_PI; }
double deg2rad(double x) { return x * pi() / 180; }
double rad2deg(double x) { return x * 180 / pi(); }

// Checks if the SocketIO event has JSON data.
// If there is data the JSON object in string format will be returned,
// else the empty string "" will be returned.
string hasData(string s) {
  auto found_null = s.find("null");
  auto b1 = s.find_first_of("[");
  auto b2 = s.rfind("}]");
  if (found_null != string::npos) {
    return "";
  } else if (b1 != string::npos && b2 != string::npos) {
    return s.substr(b1, b2 - b1 + 2);
  }
  return "";
}

// Evaluate a polynomial.
double polyeval(Eigen::VectorXd coeffs, double x) {
  double result = 0.0;
  for (int i = 0; i < coeffs.size(); i++) {
    result += coeffs[i] * pow(x, i);
  }
  return result;
}

// Fit a polynomial.
// Adapted from
// https://github.com/JuliaMath/Polynomials.jl/blob/master/src/Polynomials.jl#L676-L716
Eigen::VectorXd polyfit(Eigen::VectorXd xvals, Eigen::VectorXd yvals,
                        int order) {
  assert(xvals.size() == yvals.size());
  assert(order >= 1 && order <= xvals.size() - 1);
  Eigen::MatrixXd A(xvals.size(), order + 1);

  for (int i = 0; i < xvals.size(); i++) {
    A(i, 0) = 1.0;
  }

  for (int j = 0; j < xvals.size(); j++) {
    for (int i = 0; i < order; i++) {
      A(j, i + 1) = A(j, i) * xvals(j);
    }
  }

  auto Q = A.householderQr();
  auto result = Q.solve(yvals);
  return result;
}

int main() {
  uWS::Hub h;

  // MPC is initialized here!
  MPC mpc;

  h.onMessage([&mpc](uWS::WebSocket<uWS::SERVER> ws, char *data, size_t length,
                     uWS::OpCode opCode) {
    // "42" at the start of the message means there's a websocket message event.
    // The 4 signifies a websocket message
    // The 2 signifies a websocket event
    string sdata = string(data).substr(0, length);
    cout << sdata << endl;
    if (sdata.size() > 2 && sdata[0] == '4' && sdata[1] == '2') {
      string s = hasData(sdata);
      if (s != "") {
        auto j = json::parse(s);
        string event = j[0].get<string>();
        if (event == "telemetry") {
          // j[1] is the data JSON object
          vector<double> ptsx = j[1]["ptsx"];
          vector<double> ptsy = j[1]["ptsy"];
          double px = j[1]["x"];
          double py = j[1]["y"];
          double psi = j[1]["psi"];
          double v = j[1]["speed"];
          double delta = j[1]["steering_angle"];
          double throttle = j[1]["throttle"];
          
          double a;
          // convert into SI
          v *= 0.44704;
          delta *= -1.;
          if (throttle > 0.){
            a *= 2. * throttle;
          } else {
            a *= 10. * throttle;
          }

          // consider latency
          int latency_ms = 100;
          const double Lf = 2.67;
          double latency_s = latency_ms/1000. + 0.03; // 30 ms of computational time
          px += v * cos(psi) * latency_s;
          py += v * sin(psi) * latency_s;
          psi += v * delta * latency_s / Lf;
          v += a * latency_s;


          // move into car frame
          Eigen::Matrix3f T;
          Eigen::Vector3f P;
          for (int i = 0; i < ptsx.size(); i++ )
          {
              // Transform matrix from global to vehicle
              T << cos(psi), -sin(psi), px,
              sin(psi), cos(psi), py,
              0,0,1;
              P << ptsx[i], ptsy[i], 1;
              // Transform matrix from vehicle to global x position in global map.
              Eigen::Vector3f trans_p = T.inverse()*P;
              ptsx[i] = trans_p[0];
              ptsy[i] = trans_p[1];
          }
          std::cout << "psi" << std::endl;
          std::cout << psi << std::endl;
 
          double* ptrx = &ptsx[0];
          Eigen::Map<Eigen::VectorXd> ptsx_e(ptrx, 6);
          double* ptry = &ptsy[0];
          Eigen::Map<Eigen::VectorXd> ptsy_e(ptry, 6);
          //Eigen::VectorXd ptsx_e(ptsx.data());
          //Eigen::VectorXd ptsy_e(ptsy.data());
          auto coeffs = polyfit(ptsx_e, ptsy_e, 3);


          // The cross track error is calculated by evaluating at polynomial at x, f(x)
          // and subtracting y.
          double cte = polyeval(coeffs, 0.);
          // Due to the sign starting at 0, the orientation error is -f'(x).
          // derivative of coeffs[0] + coeffs[1] * x -> coeffs[1]
          double epsi = 0. - atan(coeffs[1]);

          std::cout << "cte" << std::endl;
          std::cout << cte << std::endl;
          std::cout << "epsi" << std::endl;
          std::cout << epsi << std::endl;
          /*
          * TODO: Calculate steering angle and throttle using MPC.
          *
          * Both are in between [-1, 1].
          *
          */
          
          Eigen::VectorXd state(6);
          state << 0, 0, 0, v, cte, epsi;
          
          std::vector<double> x_vals = {state[0]};
          std::vector<double> y_vals = {state[1]};
          std::vector<double> psi_vals = {state[2]};
          std::vector<double> v_vals = {state[3]};
          std::vector<double> cte_vals = {state[4]};
          std::vector<double> epsi_vals = {state[5]};
          std::vector<double> delta_vals = {};
          std::vector<double> a_vals = {};

          auto t1 = std::chrono::high_resolution_clock::now();
        
          auto vars = mpc.Solve(state, coeffs);

          auto t2 = std::chrono::high_resolution_clock::now();
          std::cout << "Delta t2-t1: " 
              << std::chrono::duration_cast<std::chrono::milliseconds>(t2- t1).count()
              << " milliseconds" << std::endl;

          double steer_value = vars[0];
          double accel_value = vars[1];
          double throttle_value;

          if (accel_value > 0.) {
            throttle_value = accel_value/2.;  // gas
          } else {
            throttle_value = accel_value/10.;  // gas
          }

          json msgJson;
          // NOTE: Remember to divide by deg2rad(25) before you send the steering value back.
          // Otherwise the values will be in between [-deg2rad(25), deg2rad(25] instead of [-1, 1].
          msgJson["steering_angle"] = -steer_value/deg2rad(25);
          msgJson["throttle"] = throttle_value;

          //Display the MPC predicted trajectory 
          vector<double> mpc_x_vals(mpc.pred_x.size());
          vector<double> mpc_y_vals(mpc.pred_y.size());
          // glob_to_local(mpc.pred_x, mpc.pred_y, psi, px, py, mpc_x_vals, mpc_y_vals);
          mpc_x_vals = mpc.pred_x;
          mpc_y_vals = mpc.pred_y;

          //.. add (x,y) points to list here, points are in reference to the vehicle's coordinate system
          // the points in the simulator are connected by a Green line

          msgJson["mpc_x"] = mpc_x_vals;
          msgJson["mpc_y"] = mpc_y_vals;

          //Display the waypoints/reference line
          vector<double> next_x_vals = ptsx;
          vector<double> next_y_vals = ptsy;

          //vector<double> next_x_vals(mpc.ref_x.size());
          //vector<double> next_y_vals(mpc.ref_y.size());
          // glob_to_local(mpc.pred_x, mpc.pred_y, psi, px, py, mpc_x_vals, mpc_y_vals);
          //next_x_vals = mpc.ref_x;
          //next_y_vals = mpc.ref_y;

          //.. add (x,y) points to list here, points are in reference to the vehicle's coordinate system
          // the points in the simulator are connected by a Yellow line

          msgJson["next_x"] = next_x_vals;
          msgJson["next_y"] = next_y_vals;


          auto msg = "42[\"steer\"," + msgJson.dump() + "]";
          std::cout << msg << std::endl;
          // Latency
          // The purpose is to mimic real driving conditions where
          // the car does actuate the commands instantly.
          //
          // Feel free to play around with this value but should be to drive
          // around the track with 100ms latency.
          //
          // NOTE: REMEMBER TO SET THIS TO 100 MILLISECONDS BEFORE
          // SUBMITTING.
          this_thread::sleep_for(chrono::milliseconds(latency_ms));
          
          ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
        }
      } else {
        // Manual driving
        std::string msg = "42[\"manual\",{}]";
        ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
      }
    }
  });

  // We don't need this since we're not using HTTP but if it's removed the
  // program
  // doesn't compile :-(
  h.onHttpRequest([](uWS::HttpResponse *res, uWS::HttpRequest req, char *data,
                     size_t, size_t) {
    const std::string s = "<h1>Hello world!</h1>";
    if (req.getUrl().valueLength == 1) {
      res->end(s.data(), s.length());
    } else {
      // i guess this should be done more gracefully?
      res->end(nullptr, 0);
    }
  });

  h.onConnection([&h](uWS::WebSocket<uWS::SERVER> ws, uWS::HttpRequest req) {
    std::cout << "Connected!!!" << std::endl;
  });

  h.onDisconnection([&h](uWS::WebSocket<uWS::SERVER> ws, int code,
                         char *message, size_t length) {
    ws.close();
    std::cout << "Disconnected" << std::endl;
  });

  int port = 4567;
  if (h.listen(port)) {
    std::cout << "Listening to port " << port << std::endl;
  } else {
    std::cerr << "Failed to listen to port" << std::endl;
    return -1;
  }
  h.run();
}
