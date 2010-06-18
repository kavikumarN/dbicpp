#include "dbic++.h"

using namespace std;
using namespace dbi;

int main() {
    // Handle h = dbiConnect("driver", "user", "password", "database", "host", "port");
    Handle h = dbiConnect("postgresql", "udbicpp", "", "dbicpp");
    Statement st(h, "SELECT id, name, email FROM users WHERE id >= ? AND id < ?");

    // bind and execute the statement in one go.
    st % 1L, 10L, execute();
    ResultRow r;
    while ((r = st.fetchRow()).size() > 0) {
        cout << r.join("\t") << endl;
    }
    st.finish();

    // bind and execute the statement later.
    st % 1L, 10L;
    st.execute();
    ResultRowHash rh;
    while ((rh = st.fetchRowHash()).size() > 0) {
        cout << rh["name"] << endl;
    }
    st.finish();

    h.begin("transaction_1");
    h.execute("DELETE FROM users WHERE id = 1");
    h.begin("transaction_2");
    h.execute("DELETE FROM users WHERE id = 2");
    h.rollback("transaction_2");
    h.commit("transaction_1");
}
