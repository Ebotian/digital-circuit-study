module tb;
  logic [15:0] A;
  logic [4:0] LZ, LO, TZ, TO;
  top dut(.A(A), .LZ(LZ), .LO(LO), .TZ(TZ), .TO(TO));
  int fail = 0;
  task check(input [15:0] a, input [4:0] lz, lo, tz, to);
    A = a; #1;
    if (LZ !== lz || LO !== lo || TZ !== tz || TO !== to) begin
      $display("FAIL %04x: LZ=%0d/%0d LO=%0d/%0d TZ=%0d/%0d TO=%0d/%0d",
               a, LZ, lz, LO, lo, TZ, tz, TO, to);
      fail++;
    end
  endtask
  initial begin
    check(16'h4008, 1, 0, 3, 0);   // 16392
    check(16'h0000, 16, 0, 16, 0);
    check(16'hFFFF, 0, 16, 0, 16);
    check(16'h8000, 0, 1, 15, 0);
    check(16'h0001, 15, 0, 0, 1);
    check(16'hFFF8, 0, 13, 3, 0);
    if (fail == 0) $display("PASS lzcount16");
    $finish;
  end
endmodule
