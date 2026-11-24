module golden(
  input logic A, B, CIN,
  output logic S, COUT);
  assign S = A ^ B ^ CIN;
  assign COUT = (A & B) | (A & CIN) | (B & CIN);
endmodule
