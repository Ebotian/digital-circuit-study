// 16-bit leading/trailing zero/one count via four 16-4 priority encoders.
// p(A) = index (0..15) of the highest set bit of A.  For 4-bit p, 15-p = ~p.
module top(
  input logic [15:0] A,
  output logic [4:0] LZ, LO, TZ, TO);
  wire [3:0] pz, po, ptz, pto;
  wire all_zero = ~(|A);        // A == 0
  wire all_one  = ~(|~A);       // A == 0xFFFF
  // leading: highest 1 of A / highest 0 of A
  pe16 ez(.A15(A[15]), .A14(A[14]), .A13(A[13]), .A12(A[12]),
          .A11(A[11]), .A10(A[10]), .A9(A[9]),   .A8(A[8]),
          .A7(A[7]),   .A6(A[6]),   .A5(A[5]),   .A4(A[4]),
          .A3(A[3]),   .A2(A[2]),   .A1(A[1]),   .A0(A[0]),
          .Y3(pz[3]), .Y2(pz[2]), .Y1(pz[1]), .Y0(pz[0]));
  pe16 eo(.A15(~A[15]), .A14(~A[14]), .A13(~A[13]), .A12(~A[12]),
          .A11(~A[11]), .A10(~A[10]), .A9(~A[9]),   .A8(~A[8]),
          .A7(~A[7]),   .A6(~A[6]),   .A5(~A[5]),   .A4(~A[4]),
          .A3(~A[3]),   .A2(~A[2]),   .A1(~A[1]),   .A0(~A[0]),
          .Y3(po[3]), .Y2(po[2]), .Y1(po[1]), .Y0(po[0]));
  // trailing: same detector on the bit-reversed word
  pe16 ezr(.A15(A[0]),  .A14(A[1]),  .A13(A[2]),  .A12(A[3]),
           .A11(A[4]),  .A10(A[5]),  .A9(A[6]),   .A8(A[7]),
           .A7(A[8]),   .A6(A[9]),   .A5(A[10]),  .A4(A[11]),
           .A3(A[12]),  .A2(A[13]),  .A1(A[14]),  .A0(A[15]),
           .Y3(ptz[3]), .Y2(ptz[2]), .Y1(ptz[1]), .Y0(ptz[0]));
  pe16 eor(.A15(~A[0]),  .A14(~A[1]),  .A13(~A[2]),  .A12(~A[3]),
           .A11(~A[4]),  .A10(~A[5]),  .A9(~A[6]),   .A8(~A[7]),
           .A7(~A[8]),   .A6(~A[9]),   .A5(~A[10]),  .A4(~A[11]),
           .A3(~A[12]),  .A2(~A[13]),  .A1(~A[14]),  .A0(~A[15]),
           .Y3(pto[3]), .Y2(pto[2]), .Y1(pto[1]), .Y0(pto[0]));
  assign LZ = all_zero ? 5'd16 : {1'b0, ~pz};
  assign LO = all_one  ? 5'd16 : {1'b0, ~po};
  assign TZ = all_zero ? 5'd16 : {1'b0, ~ptz};
  assign TO = all_one  ? 5'd16 : {1'b0, ~pto};
endmodule
