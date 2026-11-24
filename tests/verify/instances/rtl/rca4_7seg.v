// 4-bit ripple-carry adder with two hex input displays, a hex sum display,
// and a carry-out LED.  Built from fa1 and hex7seg atoms.
module top(
  input logic [3:0] A, B,
  output logic LED,
  output logic SA_a, SA_b, SA_c, SA_d, SA_e, SA_f, SA_g,
  output logic SB_a, SB_b, SB_c, SB_d, SB_e, SB_f, SB_g,
  output logic SS_a, SS_b, SS_c, SS_d, SS_e, SS_f, SS_g);
  wire [3:0] S;
  wire [4:0] C;
  assign C[0] = 1'b0;
  fa1 f0(.A(A[0]), .B(B[0]), .CIN(C[0]), .S(S[0]), .COUT(C[1]));
  fa1 f1(.A(A[1]), .B(B[1]), .CIN(C[1]), .S(S[1]), .COUT(C[2]));
  fa1 f2(.A(A[2]), .B(B[2]), .CIN(C[2]), .S(S[2]), .COUT(C[3]));
  fa1 f3(.A(A[3]), .B(B[3]), .CIN(C[3]), .S(S[3]), .COUT(C[4]));
  assign LED = C[4];
  hex7seg da(.A(A[3]), .B(A[2]), .C(A[1]), .D(A[0]),
             .seg_a(SA_a), .seg_b(SA_b), .seg_c(SA_c), .seg_d(SA_d),
             .seg_e(SA_e), .seg_f(SA_f), .seg_g(SA_g));
  hex7seg db(.A(B[3]), .B(B[2]), .C(B[1]), .D(B[0]),
             .seg_a(SB_a), .seg_b(SB_b), .seg_c(SB_c), .seg_d(SB_d),
             .seg_e(SB_e), .seg_f(SB_f), .seg_g(SB_g));
  hex7seg ds(.A(S[3]), .B(S[2]), .C(S[1]), .D(S[0]),
             .seg_a(SS_a), .seg_b(SS_b), .seg_c(SS_c), .seg_d(SS_d),
             .seg_e(SS_e), .seg_f(SS_f), .seg_g(SS_g));
endmodule
