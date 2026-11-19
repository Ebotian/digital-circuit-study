module xor_chain (
  input  wire clk,
  input  wire a,
  input  wire b,
  output reg  y
);
  reg t1, t2, t3;
  always @(posedge clk) begin
    t1 <= a  ^ b;
    t2 <= t1 ^ b;
    t3 <= t2 ^ b;
    y  <= t3;
  end
endmodule
