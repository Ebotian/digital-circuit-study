module tb;
  logic A, B;
  logic Yg, okg, Yb, okb;

  xor6_sw #(.BREAK(1'b0)) dut_good (.A(A), .B(B), .Y(Yg), .ok(okg));
  xor6_sw #(.BREAK(1'b1)) dut_bad  (.A(A), .B(B), .Y(Yb), .ok(okb));

  int failg = 0, failb = 0, rows = 0;

  initial begin
    $display("");
    $display("  A B | XOR 期望 |  正例: Y ok |  负例: Y ok   (负例=把 N3 栅极错接到 GND)");
    $display(" -----+----------+-------------+------------------------------------");
    for (int i = 0; i < 4; i++) begin
      A = i[1]; B = i[0]; #1ns;
      begin
        logic exp; exp = A ^ B;
        if (!okg || (Yg !== exp)) failg++;
        if (!okb || (Yb !== exp)) failb++;
        $display("   %0d %0d |    %0d     |   %0d  %0d     |   %0d  %0d   %s",
                 A, B, exp, Yg, okg, Yb, okb,
                 (!okb || (Yb !== exp)) ? "<-- 被检出" : "");
        rows++;
      end
    end
    $display("");
    $display(" 正例失败 %0d / %0d 行   ->  %s", failg, rows, (failg==0) ? "PASS" : "FAIL");
    $display(" 负例失败 %0d / %0d 行   ->  %s", failb, rows, (failb>0)  ? "PASS(检查器真的能报错)" : "BUG(检查器是摆设)");
    $display("");
    $finish;
  end
endmodule
