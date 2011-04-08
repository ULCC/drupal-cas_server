<?php
// $Id: cas_server.pages.inc,v 1.1 2009/10/08 20:51:58 jtenman Exp $ 

/**
 * The callback when someone visits cas_server/login
 */
function cas_server_login_page() {
  global $user;
  
  $service = urldecode($_GET['service']);
  $renew   = isset($_GET['renew']) ? $_GET['renew'] : false;
  $gateway = isset($_GET['gateway']) ? $_GET['gateway'] : false;
  // if $warn is not set, use the session one if it's there.
  $warn = isset($_GET['warn']) || (isset($_SESSION['cas']['warn']) ? $_SESSION['cas']['warn'] : false);

  return cas_server_login_request($service, $warn, $renew, $gateway);
}

/**
 * Function callback for server_logout
 */
function cas_server_logout_page() {
  global $user;
  
  //Log the user out of the cas_server
  if ($user->uid && isset($_SESSION['cas']['current_tgc'])) {

    setcookie(CAS_SERVER_COOKIE_NAME, FALSE);
    unset($_SESSION['cas']['current_tgc']);
    
    //On service logout we need invalidate all valid tickets
    db_query('UPDATE {cas_server_service} SET current_valid_ticket = 0 WHERE uid = %d', $user->uid);
    db_query('UPDATE {cas_server_proxy_granting} cspg, {cas_server_service} css SET cspg.current_valid_ticket = 0 WHERE css.uid = %d AND css.service_ticket_id=cspg.service_ticket_id', $user->uid);
    db_query('UPDATE {cas_server_proxy} csp, {cas_server_proxy_granting} cspg, {cas_server_service} css SET csp.current_valid_ticket = 0 WHERE css.uid = %d AND css.service_ticket_id = cspg.service_ticket_id AND csp.proxy_granting_ticket_id = cspg.proxy_granting_ticket_id', $user->uid);
    
    if (isset($_GET['url'])) {
      $url = urldecode($_GET['url']);
            $message = 'The application you just logged out of has provided a link it would like you to follow. Please !link to access @url';
            drupal_set_message(t($message, array(
                  '!link' => l('click here', $url, array(
                          'absolute' => TRUE)), 
                  '@url' => url($url, array(
                          'absolute' => TRUE)))));
    }
    else {
      drupal_set_message(t('Successfully logged out of service'));
    }
  }
  
  
    $destination = isset($_GET['url']) ? $_GET['url'] : null;
    
    session_destroy();
    module_invoke_all('user', 'logout', NULL, $user);

    // Load the anonymous user
    $user = drupal_anonymous_user();
    
    drupal_goto($destination);
  
}

/**
 * The callback when someone visits cas_server/validate or cas_server/serviceValidate or cas_server/proxyValidate
 * $v - The version of the request. Currently 1.0 and 2.0 supported. And S1 (saml) - Matt
 * $proxy_check - Whether or not to check if the ticket is a proxy ticket or not
 */
function cas_server_validate_page($v, $proxy_check = FALSE, $ticket=false) {

  $service = urldecode($_GET['service']);
  if (!$service) {
      // saml special case
      $service = urldecode($_GET['TARGET']);
  }
  $renew   = $_GET['renew'];
  $sticket = (empty($ticket)) ? $_GET['ticket'] : $ticket;
  $pgtUrl = urldecode($_GET['pgtUrl']);
  $proxy = FALSE;
  
  if (!$service) {
    return _cas_server_validate_response($v, array('code' => 'INVALID_REQUEST', 'error' => 'The following parameters are required: ticket, service, but service was not supplied'));
  }

  if (!$sticket) {
    return _cas_server_validate_response($v, array('code' => 'INVALID_REQUEST', 'error' => 'The following parameters are required: ticket, service, but ticket was not supplied'));
  }
  
  //Check if this is a proxy call
  if ($proxy_check && (drupal_substr($sticket, 0, 2) == 'PT')) {
    $proxy = TRUE;
  }
  
  if (($ticket = cas_server_load_ticket_and_invalidate($service, $sticket, $renew, $proxy)) && is_object($ticket)) {
    $which = variable_get('cas_server_username_value', 'name');
    $ticket->username = $ticket->{$which};

    // TODO can this be used to hook in and alter the user object to include other stuff?
    drupal_alter('cas_server_ticket', $ticket); //Allow any module to modify the username

    $response = array(
            'success' => $ticket->username
        );

    // TODO added - does it break other methods?
    $ticket->success = $ticket->username;
    // saml neds this
    $ticket->service = $service;
    
    if (!$proxy && $pgtUrl) {
      cas_server_handle_pgt_request($pgtUrl, $ticket, $response);
    }
    // Moodle calls this one. Was response before.
    return _cas_server_validate_response($v, (array)$ticket);
  }
  else {
    // ticket wil be false
    return _cas_server_validate_response($v, $ticket);
  }
}

/**
 * Need to get the ticket from the saml soap request
 *
 * @param <type> $v
 * @param <type> $proxy_check
 */
function cas_server_validate_saml_page($v, $proxy_check = FALSE) {

    // What the request looks like:
    //
    //    <SOAP-ENV:Envelope xmlns:SOAP-ENV="http://schemas.xmlsoap.org/soap/envelope/">
    //        <SOAP-ENV:Header/>
    //        <SOAP-ENV:Body>
    //            <samlp:Request xmlns:samlp="urn:oasis:names:tc:SAML:1.0:protocol"  MajorVersion="1" MinorVersion="1" RequestID="_192.168.16.51.1024506224022" IssueInstant="2002-06-19T17:03:44.022Z">
    //                <samlp:AssertionArtifact>ST-5CqHj8TnPr8YFbCQb4BMA7k2N9Hpi</samlp:AssertionArtifact>
    //            </samlp:Request>
    //        </SOAP-ENV:Body>
    //    </SOAP-ENV:Envelope>

   $server = new SoapServer(null, array('uri' => "http://test-uri/"));
   $server->setObject(new samlResponse());
   $server->handle();
}

/**
 * The callback when someone visits cas_server/proxy
 */
function cas_server_proxy_page() {
  $targetService  = urldecode($_GET['targetService']);
  $pgt   = $_GET['pgt'];
  
  $response = array('proxy' => TRUE);
  if (!$targetService || !$pgt) {
    $response['code'] = 'INVALID_REQUEST';
    $response['error'] = '"pgt" and "targetService" parameters are both required';
  }
  elseif (!$pgt_ticket = cas_server_load_proxy_granting_ticket($pgt)) {
    $response['code'] = 'INVALID_TICKET';
    $response['error'] = 'Ticket '. $pgt .'is not recognized';
  }
  else if ($pgt_ticket->service_url != $targetService) {
    watchdog('cas_server', 'PROXY request invalid. The given proxy granting ticket (@pgt) does not belong to the target service (@service).', array('@pgt' => $$pgt, '@service' => $targetService), WATCHDOG_WARNING);
    $response['code'] = 'INVALID_TICKET';
    $response['error'] = 'Ticket '. $pgt .' does not match with '. $targetService;
  }
  else {
      
    $proxy_ticket = cas_server_new_proxy_ticket($pgt, $targetService);

    if ($proxy_ticket) {
      $response['success'] = $proxy_ticket->proxy_ticket;
    }
    else {
      $response['code'] = 'INTERNAL_ERROR';
      $response['error'] = 'An internal error occurred while granting proxy ticket';
    }
  }
  
  _cas_server_validate_response('2.0', $response);
}

/**
 * Handle getting a proxy granting ticket
 *
 * $pgtUrl - The url to pass the proxy granting ticket id and iou to
 * $ticket - The service ticket that is being used to obtain this proxy
 * $response - Array that can be used to store the results to return to the service
 */
function cas_server_handle_pgt_request($pgtUrl, $ticket, &$response) {
  $type = strtolower(drupal_substr($pgtUrl, 0, 5));
  if ($type != 'https') { //This URL MUST be HTTPS
    return FALSE;
  }
  
  $headers = array();
  $pgt = _cas_server_generate_ticket('PGT', 64);
  $pgt_iou = _cas_server_generate_ticket('PGTIOU', 64);
  $url = $pgtUrl .'?pgtId='. $pgt .'&pgtIou=' . $pgt_iou;
  
  //CAS MUST verify both that the SSL certificate is valid and that its name matches that of the service
  $headers[CURLOPT_SSL_VERIFYPEER] = TRUE;
  $headers[CURLOPT_SSL_VERIFYHOST] = 2;
  
  $headers[CURLOPT_URL] = $url;
  $headers[CURLOPT_RETURNTRANSFER] = TRUE;
  //5 seconds to connect. If it takes longer then too bad
  $headers[CURLOPT_CONNECTTIMEOUT] = 5;
  //8 seconds is an eternity to complete this simple request
  $headers[CURLOPT_TIMEOUT] = 8;
  $headers[CURLOPT_FOLLOWLOCATION] = TRUE;
  $headers[CURLOPT_MAXREDIRS] = 15;
  
  $ch = curl_init();
  curl_setopt_array($ch, $headers);
  $output = trim(curl_exec($ch));
  $code = curl_getinfo($ch, CURLINFO_HTTP_CODE);
  curl_close($ch);
  
  if ($code == 200) {
    $proxy_granting_ticket = array('service_ticket_id' => $ticket->service_ticket_id, 'callback_url' => $pgtUrl, 'proxy_ticket' => $pgt, 'proxy_ticket_iou' => $pgt_iou, 'current_valid_ticket' => 1);
    drupal_write_record('cas_server_proxy_granting', $proxy_granting_ticket);
    $response['pgt'] = $pgt_iou;
    $response['proxies'] = array($pgtUrl);
  }
  return FALSE;
}

/**
 * Callback to output the response in the correct format. This function does not return.
 * It outputs the results using 'echo' and then exits
 *
 * $v - The version of the request
 * $value - An array that can contain the following:
 *   'success' - If this value is set, it should be the username to be returned or the proxy ticket if proxy is set
 *   'proxy'   - If the request is to obtain a proxy ticket, this should be set to TRUE
 *   'pgt'     - The proxy granting cookie to return
 *   'proxies' - An array of proxies used to provide the proxy granting cookie
 *   'code'    - If the request was not successful, the error code to be returned
 *   'error'   - Descriptive text of what the actual error was
 */
function _cas_server_validate_response($v, $value) {

    switch ($v) {

        case '1.0':

            if ($value['success']) {
              echo "yes\n". $value['success'] ."\n";
            } else {
              echo "no\n\n";
            }
            break;

        case 'S1':
            header("Content-type: text/xml");
            echo _cas_server_validate_response_saml($value);
            break;

        default:
            header("Content-type: text/xml");
            echo _cas_server_validate_respnse_xml($value);
    }

    exit();
}

/**
 * Processes the ticket to create a SAML response. Not sure what the saml responseid should be...
 * 
 * @param array $results the ticket with attributes
 * @return string the SAML document
 */
function _cas_server_validate_response_saml($results) {

    $result = '
        
        <SOAP-ENV:Envelope xmlns:SOAP-ENV="http://schemas.xmlsoap.org/soap/envelope/">
          <SOAP-ENV:Header />
          <SOAP-ENV:Body>
            <Response xmlns="urn:oasis:names:tc:SAML:1.0:protocol" xmlns:saml="urn:oasis:names:tc:SAML:1.0:assertion"
            xmlns:samlp="urn:oasis:names:tc:SAML:1.0:protocol" xmlns:xsd="http://www.w3.org/2001/XMLSchema"
            xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" IssueInstant="2008-12-10T14:12:14.817Z"
            MajorVersion="1" MinorVersion="1" Recipient="'.$results['service'].'"
            ResponseID="_5c94b5431c540365e5a70b2874b75996">
              <Status>
                <StatusCode Value="samlp:Success">
                </StatusCode>
              </Status>
              <Assertion xmlns="urn:oasis:names:tc:SAML:1.0:assertion" AssertionID="_e5c23ff7a3889e12fa01802a47331653"
              IssueInstant="2008-12-10T14:12:14.817Z" Issuer="'.base_path().'" MajorVersion="1"
              MinorVersion="1">
                <Conditions NotBefore="'.date('o-m-d', time()).'T'.date('H:i:-m-d', time()).'.817Z" NotOnOrAfter="'.date('o-m-d', time()).'T'.date('H:i:-m-d', time()).'.817Z">
                  <AudienceRestrictionCondition>
                    <Audience>
                      '.$results['service'].'
                    </Audience>
                  </AudienceRestrictionCondition>
                </Conditions>
                <AttributeStatement>
                  <Subject>
                    <NameIdentifier>
                      '.$results['success'].'
                    </NameIdentifier>
                    <SubjectConfirmation>
                      <ConfirmationMethod>
                        urn:oasis:names:tc:SAML:1.0:cm:artifact
                      </ConfirmationMethod>
                    </SubjectConfirmation>
                  </Subject>
                  <Attribute AttributeName="uid" AttributeNamespace="http://www.ja-sig.org/products/cas/">
                    <AttributeValue>
                      '.$results['uid'].'
                    </AttributeValue>
                  </Attribute>';

                if (isset($results['mail'])) {
                    $result .= '
                  <Attribute AttributeName="mail" AttributeNamespace="http://www.ja-sig.org/products/cas/">
                    <AttributeValue>
                      '.$results['mail'].'
                    </AttributeValue>
                  </Attribute>';
                }
                
                if (isset($results['firstname'])) {

                  $result .= '
                <Attribute AttributeName="firstname" AttributeNamespace="http://www.ja-sig.org/products/cas/">
                  <AttributeValue>
                    '.$results['firstname'].'
                  </AttributeValue>
                </Attribute>';

                }

                if (isset($results['lastname'])) {

                  $result .= '
                <Attribute AttributeName="lastname" AttributeNamespace="http://www.ja-sig.org/products/cas/">
                  <AttributeValue>
                    '.$results['lastname'].'
                  </AttributeValue>
                </Attribute>';

                }
                 
                $result .= '
                    </AttributeStatement>
                <AuthenticationStatement AuthenticationInstant="'.date('o-m-d', time()).'T'.date('H:i:-m-d', time()).'.741Z"
                AuthenticationMethod="urn:oasis:names:tc:SAML:1.0:am:password">
                  <Subject>
                    <NameIdentifier>
                      '.$results['success'].'
                    </NameIdentifier>
                    <SubjectConfirmation>
                      <ConfirmationMethod>
                        urn:oasis:names:tc:SAML:1.0:cm:artifact
                      </ConfirmationMethod>
                    </SubjectConfirmation>
                  </Subject>
                </AuthenticationStatement>
              </Assertion>
            </Response>
          </SOAP-ENV:Body>
        </SOAP-ENV:Envelope>';

    return $result;
}

/**
 * XML creator for the version 2.0 responses. See _cas_server_validate_response()
 * for values of the $results array.
 */
function _cas_server_validate_respnse_xml($results) {
  $result = "<?xml version=\"1.0\"?>\n";
  $result .= "<cas:serviceResponse xmlns:cas=\"http://www.yale.edu/tp/cas\">\n";
  if ($results['proxy']) {
    if ($results['success']) {
      $result .= "  <cas:proxySuccess>\n";
      $result .= "    <cas:proxyTicket>". $results['success'] ."</cas:proxyTicket>\n";
      $result .= "  </cas:proxySuccess>\n";
    } else {
      $result .= "  <cas:proxyFailure code=\"". $results['code'] ."\">". $results['error'] ."</cas:proxyFailure>\n";
    }
  }
  else {
    if ($results['success']) {
      $result .= "  <cas:authenticationSuccess>\n";
      $result .= "    <cas:user>". $results['success'] ."</cas:user>\n";
      if ($results['pgt']) {
        $result .= "    <cas:proxyGrantingTicket>". $results['pgt'] ."</cas:proxyGrantingTicket>\n";
      }
      if ($results['proxies']) {
        $result .= "    <cas:proxies>\n";
        foreach ($results['proxies'] as $proxy) $result .= "      <cas:proxy>". $proxy ."<cas:proxy>\n";
        $result .= "    </cas:proxies>\n";
      }
      $result .= "  </cas:authenticationSuccess>\n";
    } else {
      $result .= "  <cas:authenticationFailure code=\"". $results['code'] ."\">". $results['error'] ."</cas:authenticationFailure>\n";
    }
  }
  $result .= "</cas:serviceResponse>\n";
  return $result;
}

/**
 * This allows Moodle to get the basic userdata to create an account when the user first
 * logs in. It presents the data in JSON format if the user is logged in and nothing otherwise
 *
 * @global <type> $user
 * @return <type>
 */
function cas_server_userdata_page() {

    // permissions check?
    // right now, it only works for the data of the logged in user, so should be fine

    global $user;

    $extendeduser = user_load(array(uid => $user->uid));

    if (!$user->uid) {
        return;
    }

    $userdata = new stdClass;
    $userdata->username  = $extendeduser->name;
    $userdata->email     = $extendeduser->mail;
    $userdata->firstname = $extendeduser->profile_forenames;
    $userdata->lastname  = $extendeduser->profile_surname;



//    header('Cache-Control: no-cache, must-revalidate');
//    header('Expires: Mon, 26 Jul 2011 05:00:00 GMT');
    header('Content-type: text/javascript');
   // header('Content-Transfer-Encoding: binary');

   // echo '<html>';
    print drupal_to_js($userdata);
   // echo json_encode($userdata);
   // echo '</html>';
    //exit();
}

/**
 * This acts as the callback for the saml soap request
 */
class samlResponse {

    function Request($AssertionArtefact) {
        cas_server_validate_page('S1', FALSE, $AssertionArtefact);
    }

}