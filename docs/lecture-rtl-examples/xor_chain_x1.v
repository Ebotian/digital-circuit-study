module xor_chain (
  input  wire clk, a, b,
  output reg  y
);
  wire t1, t2, t3;
  XOR2_X1 u1 (.A(a),  .B(b), .Z(t1));
  XOR2_X1 u2 (.A(t1), .B(b), .Z(t2));
  XOR2_X1 u3 (.A(t2), .B(b), .Z(t3));
  always @(posedge clk) y <= t3;
endmodule
