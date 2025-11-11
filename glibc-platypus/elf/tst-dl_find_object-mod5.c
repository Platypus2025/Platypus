/* Slightly larger to get different layouts.  */
char mod5_data[4096];

__attribute__((annotate("callback_maybe")))
void
mod5_function (void (*f) (void))
{
  /* Make sure this is not a tail call and unwind information is
     therefore needed.  */
  f ();
  f ();
}
