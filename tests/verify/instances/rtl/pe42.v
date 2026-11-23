module pe42(input logic A3, A2, A1, A0, output logic Y1, Y0);
  assign Y1 = A3 | A2;
  assign Y0 = A3 | (~A2 & A1);
endmodule
