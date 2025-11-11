char mod7_data;

__attribute__((annotate("callback_maybe")))
void
mod7_function (void (*f) (void))
{
  /* Make sure this is not a tail call and unwind information is
     therefore needed.  */
  f ();
  f ();
}
