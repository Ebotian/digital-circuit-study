module mux2(input logic A, B, S, output logic Y);
  assign Y = (S & B) | (~S & A);
endmodule
