// 3-bit 4-to-1 mux: three 2:1 muxes per bit (tree of 2:1 stages).
module top(
  input logic [2:0] D0,
  input logic [2:0] D1,
  input logic [2:0] D2,
  input logic [2:0] D3,
  input logic [1:0] S,
  output logic [2:0] Y);
  wire lo0, hi0, lo1, hi1, lo2, hi2;
  mux2 m00(.A(D0[0]), .B(D1[0]), .S(S[0]), .Y(lo0));
  mux2 m01(.A(D2[0]), .B(D3[0]), .S(S[0]), .Y(hi0));
  mux2 m02(.A(lo0),    .B(hi0),    .S(S[1]), .Y(Y[0]));
  mux2 m10(.A(D0[1]), .B(D1[1]), .S(S[0]), .Y(lo1));
  mux2 m11(.A(D2[1]), .B(D3[1]), .S(S[0]), .Y(hi1));
  mux2 m12(.A(lo1),    .B(hi1),    .S(S[1]), .Y(Y[1]));
  mux2 m20(.A(D0[2]), .B(D1[2]), .S(S[0]), .Y(lo2));
  mux2 m21(.A(D2[2]), .B(D3[2]), .S(S[0]), .Y(hi2));
  mux2 m22(.A(lo2),    .B(hi2),    .S(S[1]), .Y(Y[2]));
endmodule
