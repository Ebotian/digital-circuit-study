// 4-bit two's complement adder with overflow flag.
//   S = (A + B) mod 16,  V = C3 ^ C4  (carry into MSB differs from carry out)
// Built from four fa1 full-adder atoms; V is one XOR2 of the two top carries.
module top(
  input logic [3:0] A, B,
  output logic [3:0] S,
  output logic V);
  wire [4:0] C;
  assign C[0] = 1'b0;
  fa1 f0(.A(A[0]), .B(B[0]), .CIN(C[0]), .S(S[0]), .COUT(C[1]));
  fa1 f1(.A(A[1]), .B(B[1]), .CIN(C[1]), .S(S[1]), .COUT(C[2]));
  fa1 f2(.A(A[2]), .B(B[2]), .CIN(C[2]), .S(S[2]), .COUT(C[3]));
  fa1 f3(.A(A[3]), .B(B[3]), .CIN(C[3]), .S(S[3]), .COUT(C[4]));
  assign V = C[3] ^ C[4];
endmodule
