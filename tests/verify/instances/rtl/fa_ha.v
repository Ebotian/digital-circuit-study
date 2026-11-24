// 1-bit full adder from two half adders + carry OR.
module top(
  input logic A, B, CIN,
  output logic S, COUT);
  wire s1, c1, s2, c2;
  ha h1(.A(A), .B(B), .S(s1), .C(c1));
  ha h2(.A(s1), .B(CIN), .S(s2), .C(c2));
  assign S = s2;
  assign COUT = c1 | c2;
endmodule
