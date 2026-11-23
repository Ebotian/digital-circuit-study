module pe16(
  input logic A15, A14, A13, A12, A11, A10, A9, A8,
  input logic A7, A6, A5, A4, A3, A2, A1, A0,
  output logic Y3, Y2, Y1, Y0);
  wire N3 = A15|A14|A13|A12;
  wire N2 = A11|A10|A9|A8;
  wire N1 = A7|A6|A5|A4;
  wire N0 = A3|A2|A1|A0;
  wire H31, H30, H21, H20, H11, H10, H01, H00;
  pe42 e3(A15, A14, A13, A12, H31, H30);
  pe42 e2(A11, A10, A9, A8, H21, H20);
  pe42 e1(A7, A6, A5, A4, H11, H10);
  pe42 e0(A3, A2, A1, A0, H01, H00);
  assign Y3 = N3 | N2;
  assign Y2 = N3 | (~N2 & N1);
  assign Y1 = N3 ? H31 : N2 ? H21 : N1 ? H11 : H01;
  assign Y0 = N3 ? H30 : N2 ? H20 : N1 ? H10 : H00;
endmodule
