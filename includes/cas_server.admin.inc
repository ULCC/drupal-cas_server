<?php
// $Id: cas_server.admin.inc,v 1.1 2009/10/08 20:51:58 jtenman Exp $ 

function cas_server_admin_form() {
    $form = array();
    $form['cas_server_warning_box'] = array(
    '#type' => 'checkbox',
    '#title' => t('Show warning checkbox'),
    '#description' => t('The CAS server can let the logging in user decide if they would like to be warned when multiple services are authenticating them against this CAS server. By checking this box, the user will be presented with this option on the login form.'),
    '#default_value' => variable_get('cas_server_warning_box', FALSE),
    );

    $form['cas_server_username_value'] = array(
    '#type' => 'select',
    '#title' => t('Username'),
    '#options' => array('name' => t('Username'), 'mail' => t('E-mail')),
    '#description' => t('Select which of the options should be sent as the username to a service'),
    '#default_value' => variable_get('cas_server_username_value', 'name'),
    );

    // only ask what civicrm data to send over if civicrm is enabled.
    if (module_exists('cas_server_civicrm_data')) {

        // get all available CiviCRM contact fields
        if ( ! civicrm_initialize( ) ) {
            return;
        }

        include('CRM/Contact/BAO/Contact.php');

        $fields = CRM_Contact_BAO_CONTACT::exportableFields(); // Note: this returns a reference to an array

        if ($fields) {

            $form['mappings'] = array(
                '#type' => 'fieldset',
                '#tree' => FALSE,
            );

            $form['mappings']['title'] = array(
                '#type' => 'item',
                '#title' => t('Data mappings from CiviCRM'),
                '#description' => t('These are optional and will be sent along with the CAS ticket
                    validation response so that the external system can create a new account with them
                    if required.<br /><br />NOTE: email is automatically sent as \'mail\'')
            );

            $form['mappings']['cas_server_civicrm_map'] = array(
                    '#tree' => TRUE
            );

            // loop through them, presenting a checkbox for each one and a name field so they can be mapped
            // to whatever the external system wants

            $existingmappings = variable_get('cas_server_civicrm_map', null);

            foreach ($fields as $field) {

                // skip any blank ones
                if (empty($field['name'])) {
                    continue;
                }

                //$setting = 'cas_server_civicrm_data_'.$field['name'].'_mapping';
                $setting = $field['name'];

                $form['mappings']['cas_server_civicrm_map'][$setting] = array(
                        '#type' => 'textfield',
                        '#title' => t($field['name']),
                        '#description' => t('What should the CiviCRM "'.$field['name'].'" field be labelled as (empty = not sent)?'),
                        '#default_value' => isset($existingmappings[$setting]) ? $existingmappings[$setting] : ''
                );


            }
        }

  }

  // Add a submit handler that will make the settings for the mappings into an array
  //$form['#submit'][] = 'cas_server_admin_submit';

  return system_settings_form($form);
}

function cas_server_admin_form_submit() {



}